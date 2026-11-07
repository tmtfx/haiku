/*
 * Copyright 2013-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <package/hpkg/PackageFileHeapWriter.h>

#include <algorithm>
#include <new>

#include <pthread.h>
#include <unistd.h>

#include <ByteOrder.h>
#include <List.h>
#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/HPKGDefs.h>

#include <AutoDeleter.h>
#include <package/hpkg/DataReader.h>
#include <package/hpkg/PackageFileHeapReader.h>
#include <RangeArray.h>
#include <CompressionAlgorithm.h>


// minimum length of data we require before trying to compress them
static const size_t kCompressionSizeThreshold = 64;

// maximum number of threads used to compress complete chunks in parallel
static const int32 kMaxCompressionThreads = 16;

// number of chunks per compression batch per compression thread
static const int32 kChunksPerCompressionThread = 2;


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


struct PackageFileHeapWriter::Chunk {
	uint64	offset;
	uint32	compressedSize;
	uint32	uncompressedSize;
	void*	buffer;
};


struct PackageFileHeapWriter::ChunkSegment {
	ssize_t	chunkIndex;
	uint32	toKeepOffset;
	uint32	toKeepSize;
};


struct PackageFileHeapWriter::ChunkBuffer {
	ChunkBuffer(PackageFileHeapWriter* writer, size_t bufferSize)
		:
		fWriter(writer),
		fChunks(),
		fCurrentChunkIndex(0),
		fNextReadIndex(0),
		fSegments(),
		fCurrentSegmentIndex(0),
		fBuffers(),
		fUnusedBuffers(),
		fBufferSize(bufferSize)
	{
	}

	~ChunkBuffer()
	{
		for (int32 i = 0; void* buffer = fBuffers.ItemAt(i); i++)
			free(buffer);
	}

	bool PushChunkSegment(uint64 chunkOffset, uint32 compressedSize,
		uint32 uncompressedSize, uint32 toKeepOffset, uint32 toKeepSize)
	{
		ChunkSegment segment;
		segment.toKeepOffset = toKeepOffset;
		segment.toKeepSize = toKeepSize;

		// might refer to the last chunk
		segment.chunkIndex = fChunks.Count() - 1;

		if (segment.chunkIndex < 0
			|| fChunks.ElementAt(segment.chunkIndex).offset != chunkOffset) {
			// no, need to push a new chunk
			segment.chunkIndex++;

			Chunk chunk;
			chunk.offset = chunkOffset;
			chunk.compressedSize = compressedSize;
			chunk.uncompressedSize = uncompressedSize;
			chunk.buffer = NULL;
			if (!fChunks.Add(chunk))
				return false;
		}

		return fSegments.Add(segment);
	}

	bool IsEmpty() const
	{
		return fSegments.IsEmpty();
	}

	bool HasMoreSegments() const
	{
		return fCurrentSegmentIndex < fSegments.Count();
	}

	const ChunkSegment& CurrentSegment() const
	{
		return fSegments[fCurrentSegmentIndex];
	}

	const Chunk& ChunkAt(ssize_t index) const
	{
		return fChunks[index];
	}

	bool HasMoreChunksToRead() const
	{
		return fNextReadIndex < fChunks.Count();
	}

	bool HasBufferedChunk() const
	{
		return fCurrentChunkIndex < fNextReadIndex;
	}

	uint64 NextReadOffset() const
	{
		return fChunks[fNextReadIndex].offset;
	}

	void ReadNextChunk()
	{
		if (!HasMoreChunksToRead())
			throw status_t(B_BAD_VALUE);

		Chunk& chunk = fChunks[fNextReadIndex++];
		chunk.buffer = _GetBuffer();

		status_t error = fWriter->ReadFileData(chunk.offset, chunk.buffer,
			chunk.compressedSize);
		if (error != B_OK)
			throw error;
	}

	void CurrentSegmentDone()
	{
		// Unless the next segment refers to the same chunk, advance to the next
		// chunk.
		const ChunkSegment& segment = fSegments[fCurrentSegmentIndex++];
		if (!HasMoreSegments()
			|| segment.chunkIndex != CurrentSegment().chunkIndex) {
			_PutBuffer(fChunks[fCurrentChunkIndex++].buffer);
		}
	}

private:
	void* _GetBuffer()
	{
		if (!fUnusedBuffers.IsEmpty())
			return fUnusedBuffers.RemoveItem(fUnusedBuffers.CountItems() - 1);

		void* buffer = malloc(fBufferSize);
		if (buffer == NULL || !fBuffers.AddItem(buffer)) {
			free(buffer);
			throw std::bad_alloc();
		}

		return buffer;
	}

	void _PutBuffer(void* buffer)
	{
		if (buffer != NULL && !fUnusedBuffers.AddItem(buffer)) {
			fBuffers.RemoveItem(buffer);
			free(buffer);
		}
	}

private:
	PackageFileHeapWriter*	fWriter;

	Array<Chunk>			fChunks;
	ssize_t					fCurrentChunkIndex;
	ssize_t					fNextReadIndex;

	Array<ChunkSegment>		fSegments;
	ssize_t					fCurrentSegmentIndex;

	BList					fBuffers;
	BList					fUnusedBuffers;
	size_t					fBufferSize;
};


/*!	Compresses complete chunks in parallel.

	Chunks are collected in a batch. When the batch is full, it is handed to
	the worker threads, while the writer fills the other batch. Since every
	chunk is compressed independently, the resulting heap is identical to the
	one written sequentially, as long as the writer writes the batches in
	order.
*/
struct PackageFileHeapWriter::ChunkCompressor {
	struct Job {
		void*		data;
		void*		compressedData;
		size_t		compressedSize;
		status_t	error;
	};

	struct Batch {
		Job*		jobs;
		int32		count;
	};

	ChunkCompressor(CompressionAlgorithmOwner* compressionAlgorithm)
		:
		fCompressionAlgorithm(compressionAlgorithm),
		fThreads(NULL),
		fThreadCount(0),
		fBatchSize(0),
		fFillingBatch(0),
		fInFlightBatch(NULL),
		fNextJob(0),
		fRemainingJobs(0),
		fQuit(false)
	{
		fBatches[0].jobs = NULL;
		fBatches[0].count = 0;
		fBatches[1].jobs = NULL;
		fBatches[1].count = 0;

		pthread_mutex_init(&fLock, NULL);
		pthread_cond_init(&fWorkCondition, NULL);
		pthread_cond_init(&fDoneCondition, NULL);
	}

	~ChunkCompressor()
	{
		pthread_mutex_lock(&fLock);
		fQuit = true;
		pthread_cond_broadcast(&fWorkCondition);
		pthread_mutex_unlock(&fLock);

		for (int32 i = 0; i < fThreadCount; i++)
			pthread_join(fThreads[i], NULL);
		delete[] fThreads;

		for (int32 i = 0; i < 2; i++) {
			if (fBatches[i].jobs == NULL)
				continue;
			for (int32 k = 0; k < fBatchSize; k++) {
				free(fBatches[i].jobs[k].data);
				free(fBatches[i].jobs[k].compressedData);
			}
			delete[] fBatches[i].jobs;
		}

		pthread_cond_destroy(&fDoneCondition);
		pthread_cond_destroy(&fWorkCondition);
		pthread_mutex_destroy(&fLock);
	}

	status_t Init(int32 threadCount)
	{
		fBatchSize = threadCount * kChunksPerCompressionThread;

		for (int32 i = 0; i < 2; i++) {
			fBatches[i].jobs = new(std::nothrow) Job[fBatchSize];
			if (fBatches[i].jobs == NULL)
				return B_NO_MEMORY;

			for (int32 k = 0; k < fBatchSize; k++) {
				Job& job = fBatches[i].jobs[k];
				job.data = malloc(kChunkSize);
				job.compressedData = malloc(kChunkSize);
				if (job.data == NULL || job.compressedData == NULL) {
					// make sure the destructor frees the remaining ones
					for (k++; k < fBatchSize; k++) {
						fBatches[i].jobs[k].data = NULL;
						fBatches[i].jobs[k].compressedData = NULL;
					}
					return B_NO_MEMORY;
				}
			}
		}

		fThreads = new(std::nothrow) pthread_t[threadCount];
		if (fThreads == NULL)
			return B_NO_MEMORY;

		for (; fThreadCount < threadCount; fThreadCount++) {
			if (pthread_create(&fThreads[fThreadCount], NULL, &_WorkerEntry,
					this) != 0) {
				return B_NO_MORE_THREADS;
			}
		}

		return B_OK;
	}

	bool IsFillingBatchFull() const
	{
		return fBatches[fFillingBatch].count == fBatchSize;
	}

	bool HasQueuedChunks() const
	{
		return fInFlightBatch != NULL || fBatches[fFillingBatch].count > 0;
	}

	/*!	Adds a complete chunk to the filling batch. The given buffer is taken
		over and an unused buffer of the same size is returned in exchange.
	*/
	void* QueueChunk(void* data)
	{
		Job& job = fBatches[fFillingBatch].jobs[
			fBatches[fFillingBatch].count++];
		std::swap(job.data, data);
		return data;
	}

	/*!	Hands the filling batch to the worker threads. The previously
		submitted batch must have been collected already.
	*/
	void SubmitFillingBatch()
	{
		Batch* batch = &fBatches[fFillingBatch];
		fFillingBatch = 1 - fFillingBatch;

		pthread_mutex_lock(&fLock);
		fInFlightBatch = batch;
		fNextJob = 0;
		fRemainingJobs = batch->count;
		pthread_cond_broadcast(&fWorkCondition);
		pthread_mutex_unlock(&fLock);
	}

	/*!	Waits until the submitted batch has been compressed and returns it,
		or returns \c NULL, if no batch has been submitted. The caller has
		to reset the batch's count after processing it.
	*/
	Batch* CollectSubmittedBatch()
	{
		if (fInFlightBatch == NULL)
			return NULL;

		pthread_mutex_lock(&fLock);
		while (fRemainingJobs > 0)
			pthread_cond_wait(&fDoneCondition, &fLock);
		Batch* batch = fInFlightBatch;
		fInFlightBatch = NULL;
		pthread_mutex_unlock(&fLock);

		return batch;
	}

private:
	static void* _WorkerEntry(void* data)
	{
		((ChunkCompressor*)data)->_Worker();
		return NULL;
	}

	void _Worker()
	{
		pthread_mutex_lock(&fLock);

		while (true) {
			while (!fQuit && (fInFlightBatch == NULL
					|| fNextJob >= fInFlightBatch->count)) {
				pthread_cond_wait(&fWorkCondition, &fLock);
			}
			if (fQuit)
				break;

			Job& job = fInFlightBatch->jobs[fNextJob++];
			pthread_mutex_unlock(&fLock);

			const iovec uncompressed = { job.data, kChunkSize };
			iovec compressed = { job.compressedData, kChunkSize };
			job.error = fCompressionAlgorithm->algorithm->CompressBuffer(
				uncompressed, compressed, fCompressionAlgorithm->parameters);
			job.compressedSize = compressed.iov_len;

			pthread_mutex_lock(&fLock);
			if (--fRemainingJobs == 0)
				pthread_cond_signal(&fDoneCondition);
		}

		pthread_mutex_unlock(&fLock);
	}

private:
	CompressionAlgorithmOwner* fCompressionAlgorithm;
	pthread_t*				fThreads;
	int32					fThreadCount;
	int32					fBatchSize;

	Batch					fBatches[2];
	int32					fFillingBatch;

	pthread_mutex_t			fLock;
	pthread_cond_t			fWorkCondition;
	pthread_cond_t			fDoneCondition;
	Batch*					fInFlightBatch;
	int32					fNextJob;
	int32					fRemainingJobs;
	bool					fQuit;
};


/*!	Suspends queueing chunks for parallel compression for the lifetime of the
	object.
*/
struct ChunkQueueingSuspender {
	ChunkQueueingSuspender(bool& suspended)
		:
		fSuspended(suspended),
		fWasSuspended(suspended)
	{
		fSuspended = true;
	}

	~ChunkQueueingSuspender()
	{
		fSuspended = fWasSuspended;
	}

private:
	bool&	fSuspended;
	bool	fWasSuspended;
};


PackageFileHeapWriter::PackageFileHeapWriter(BErrorOutput* errorOutput,
	BPositionIO* file, off_t heapOffset,
	CompressionAlgorithmOwner* compressionAlgorithm,
	DecompressionAlgorithmOwner* decompressionAlgorithm)
	:
	PackageFileHeapAccessorBase(errorOutput, file, heapOffset,
		decompressionAlgorithm),
	fPendingDataBuffer(NULL),
	fCompressedDataBuffer(NULL),
	fPendingDataSize(0),
	fOffsets(),
	fCompressionAlgorithm(compressionAlgorithm),
	fChunkCompressor(NULL),
	fChunkQueueingSuspended(false)
{
	if (fCompressionAlgorithm != NULL)
		fCompressionAlgorithm->AcquireReference();
}


PackageFileHeapWriter::~PackageFileHeapWriter()
{
	_Uninit();

	if (fCompressionAlgorithm != NULL)
		fCompressionAlgorithm->ReleaseReference();
}


void
PackageFileHeapWriter::Init()
{
	// allocate data buffers
	fPendingDataBuffer = malloc(kChunkSize);
	fCompressedDataBuffer = malloc(kChunkSize);
	if (fPendingDataBuffer == NULL || fCompressedDataBuffer == NULL)
		throw std::bad_alloc();

	// Use parallel compression of complete chunks, if we compress at all and
	// have more than one CPU. If setting it up fails, we simply compress
	// sequentially.
	if (fCompressionAlgorithm == NULL)
		return;

	long cpuCount = sysconf(_SC_NPROCESSORS_ONLN);
	if (cpuCount < 2)
		return;
	int32 threadCount = std::min((int32)cpuCount, kMaxCompressionThreads);

	fChunkCompressor = new(std::nothrow) ChunkCompressor(fCompressionAlgorithm);
	if (fChunkCompressor != NULL
		&& fChunkCompressor->Init(threadCount) != B_OK) {
		delete fChunkCompressor;
		fChunkCompressor = NULL;
	}
}


void
PackageFileHeapWriter::Reinit(PackageFileHeapReader* heapReader)
{
	fHeapOffset = heapReader->HeapOffset();
	fCompressedHeapSize = heapReader->CompressedHeapSize();
	fUncompressedHeapSize = heapReader->UncompressedHeapSize();
	fPendingDataSize = 0;

	// copy the offsets array
	size_t chunkCount = (fUncompressedHeapSize + kChunkSize - 1) / kChunkSize;
	if (chunkCount > 0) {
		if (!fOffsets.AddUninitialized(chunkCount))
			throw std::bad_alloc();

		for (size_t i = 0; i < chunkCount; i++)
			fOffsets[i] = heapReader->Offsets()[i];
	}

	_UnwriteLastPartialChunk();
}


status_t
PackageFileHeapWriter::AddData(BDataReader& dataReader, off_t size,
	uint64& _offset)
{
	_offset = fUncompressedHeapSize;

	// copy the data to the heap
	off_t readOffset = 0;
	off_t remainingSize = size;
	while (remainingSize > 0) {
		// read data into pending data buffer
		size_t toCopy = std::min(remainingSize,
			off_t(kChunkSize - fPendingDataSize));
		status_t error = dataReader.ReadData(readOffset,
			(uint8*)fPendingDataBuffer + fPendingDataSize, toCopy);
		if (error != B_OK) {
			fErrorOutput->PrintError("Failed to read data: %s\n",
				strerror(error));
			return error;
		}

		fPendingDataSize += toCopy;
		fUncompressedHeapSize += toCopy;
		remainingSize -= toCopy;
		readOffset += toCopy;

		if (fPendingDataSize == kChunkSize) {
			error = _QueuePendingData();
			if (error != B_OK)
				return error;
		}
	}

	return B_OK;
}


void
PackageFileHeapWriter::AddDataThrows(const void* buffer, size_t size)
{
	BBufferDataReader reader(buffer, size);
	uint64 dummyOffset;
	status_t error = AddData(reader, size, dummyOffset);
	if (error != B_OK)
		throw status_t(error);
}


void
PackageFileHeapWriter::RemoveDataRanges(
	const ::BPrivate::RangeArray<uint64>& ranges)
{
	ssize_t rangeCount = ranges.CountRanges();
	if (rangeCount == 0)
		return;

	if (fUncompressedHeapSize == 0) {
		fErrorOutput->PrintError("Can't remove ranges from empty heap\n");
		throw status_t(B_BAD_VALUE);
	}

	// Before we begin flush any pending data, so we don't need any special
	// handling and also can use the pending data buffer.
	status_t status = _FlushPendingData();
	if (status != B_OK)
		throw status_t(status);

	// The algorithm below relies on fCompressedHeapSize to know how far ahead
	// of the writing end it has to read, so all chunks have to be written
	// immediately.
	ChunkQueueingSuspender queueingSuspender(fChunkQueueingSuspended);

	// We potentially have to recompress all data from the first affected chunk
	// to the end (minus the removed ranges, of course). As a basic algorithm we
	// can use our usual data writing strategy, i.e. read a chunk, decompress it
	// to a temporary buffer, and write the data to keep via AddData(). There
	// are a few complications/optimizations, though:
	// * As data moves to other chunks, it may actually compress worse than
	//   before. While unlikely, we still have to take care of this case by
	//   making sure our reading end is at least a complete uncompressed chunk
	//   ahead of the writing end.
	// * When we run into the situation that we have to move complete aligned
	//   chunks, we want to avoid uncompressing and recompressing them
	//   needlessly.

	// Build a list of (possibly partial) chunks we want to keep.

	// the first partial chunk (if any) and all chunks between ranges
	ChunkBuffer chunkBuffer(this, kChunkSize);
	uint64 writeOffset = ranges[0].offset - ranges[0].offset % kChunkSize;
	uint64 readOffset = writeOffset;
	for (ssize_t i = 0; i < rangeCount; i++) {
		const Range<uint64>& range = ranges[i];
		if (range.size > 0) {
			_PushChunks(chunkBuffer, readOffset, range.offset);
			readOffset = range.offset + range.size;
		}
	}

	if (readOffset == writeOffset) {
		fErrorOutput->PrintError("Only empty ranges to remove from heap\n");
		throw status_t(B_BAD_VALUE);
	}

	// all chunks after the last range
	_PushChunks(chunkBuffer, readOffset, fUncompressedHeapSize);

	// Reset our state to look like all chunks from the first affected one have
	// been removed and re-add all data we want to keep.

	// truncate the offsets array and reset the heap sizes
	ssize_t firstChunkIndex = ssize_t(writeOffset / kChunkSize);
	fCompressedHeapSize = fOffsets[firstChunkIndex];
	fUncompressedHeapSize = (uint64)firstChunkIndex * kChunkSize;
	fOffsets.Remove(firstChunkIndex, fOffsets.Count() - firstChunkIndex);

	// we need a decompression buffer
	void* decompressionBuffer = malloc(kChunkSize);
	if (decompressionBuffer == NULL)
		throw std::bad_alloc();
	MemoryDeleter decompressionBufferDeleter(decompressionBuffer);

	const Chunk* decompressedChunk = NULL;

	while (chunkBuffer.HasMoreSegments()) {
		const ChunkSegment& segment = chunkBuffer.CurrentSegment();

		// If we have an aligned, complete chunk, copy its compressed data.
		bool copyCompressed = fPendingDataSize == 0 && segment.toKeepOffset == 0
			&& segment.toKeepSize == kChunkSize;

		// Read more chunks. We need at least one buffered one to do anything
		// and we want to buffer as many as necessary to ensure we don't
		// overwrite one we haven't buffered yet.
		while (chunkBuffer.HasMoreChunksToRead()
			&& (!chunkBuffer.HasBufferedChunk()
				|| (!copyCompressed
					&& chunkBuffer.NextReadOffset()
						< fCompressedHeapSize + kChunkSize))) {
			// read chunk
			chunkBuffer.ReadNextChunk();
		}

		// copy compressed chunk data, if possible
		const Chunk& chunk = chunkBuffer.ChunkAt(segment.chunkIndex);
		if (copyCompressed) {
			status_t error = _WriteChunk(chunk.buffer, chunk.compressedSize,
				false);
			if (error != B_OK)
				throw error;
			continue;
		}

		// decompress chunk, if compressed
		void* uncompressedData;
		if (chunk.uncompressedSize == chunk.compressedSize) {
			uncompressedData = chunk.buffer;
		} else if (decompressedChunk == &chunk) {
			uncompressedData = decompressionBuffer;
		} else {
			iovec compressed = { chunk.buffer, chunk.compressedSize },
				uncompressed = { decompressionBuffer, chunk.uncompressedSize };
			status_t error = DecompressChunkData(compressed, uncompressed);
			if (error != B_OK)
				throw error;

			decompressedChunk = &chunk;
			uncompressedData = decompressionBuffer;
		}

		// add chunk data
		AddDataThrows((uint8*)uncompressedData + segment.toKeepOffset,
			segment.toKeepSize);

		chunkBuffer.CurrentSegmentDone();
	}

	// Make sure a last partial chunk ends up in the pending data buffer. This
	// is only necessary when we didn't have to move any chunk segments, since
	// the loop would otherwise have read it in and left it in the pending data
	// buffer.
	if (chunkBuffer.IsEmpty())
		_UnwriteLastPartialChunk();
}


status_t
PackageFileHeapWriter::Finish()
{
	// flush pending data, if any
	status_t error = _FlushPendingData();
	if (error != B_OK)
		return error;

	// write chunk sizes table

	// We don't need to do that, if we don't use any compression.
	if (fCompressionAlgorithm == NULL)
		return B_OK;

	// We don't need to write the last chunk size, since it is implied by the
	// total size minus the sum of all other chunk sizes.
	ssize_t offsetCount = fOffsets.Count();
	if (offsetCount < 2)
		return B_OK;

	// Convert the offsets to 16 bit sizes and write them. We use the (no longer
	// used) pending data buffer for the conversion.
	uint16* buffer = (uint16*)fPendingDataBuffer;
	for (ssize_t offsetIndex = 1; offsetIndex < offsetCount;) {
		ssize_t toWrite = std::min(offsetCount - offsetIndex,
			ssize_t(kChunkSize / 2));

		for (ssize_t i = 0; i < toWrite; i++, offsetIndex++) {
			// store chunkSize - 1, so it fits 16 bit (chunks cannot be empty)
			buffer[i] = B_HOST_TO_BENDIAN_INT16(
				uint16(fOffsets[offsetIndex] - fOffsets[offsetIndex - 1] - 1));
		}

		error = _WriteDataUncompressed(buffer, toWrite * 2);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


status_t
PackageFileHeapWriter::ReadAndDecompressChunk(size_t chunkIndex,
	void* compressedDataBuffer, void* uncompressedDataBuffer,
	iovec* scratchBuffer)
{
	// make sure all complete chunks have been written
	status_t error = _DrainCompressionQueue();
	if (error != B_OK)
		return error;

	if (uint64(chunkIndex + 1) * kChunkSize > fUncompressedHeapSize) {
		// The chunk has not been written to disk yet. Its data are still in the
		// pending data buffer.
		memcpy(uncompressedDataBuffer, fPendingDataBuffer, fPendingDataSize);
		// TODO: This can be optimized. Since we write to a BDataIO anyway,
		// there's no need to copy the data.
		return B_OK;
	}

	uint64 offset = fOffsets[chunkIndex];
	size_t compressedSize = chunkIndex + 1 == (size_t)fOffsets.Count()
		? fCompressedHeapSize - offset
		: fOffsets[chunkIndex + 1] - offset;

	return ReadAndDecompressChunkData(offset, compressedSize, kChunkSize,
		compressedDataBuffer, uncompressedDataBuffer, scratchBuffer);
}


void
PackageFileHeapWriter::_Uninit()
{
	delete fChunkCompressor;
	fChunkCompressor = NULL;

	free(fPendingDataBuffer);
	free(fCompressedDataBuffer);
	fPendingDataBuffer = NULL;
	fCompressedDataBuffer = NULL;
}


status_t
PackageFileHeapWriter::_QueuePendingData()
{
	// The pending data buffer contains a complete chunk. Queue it for parallel
	// compression, if possible.
	if (fChunkCompressor == NULL || fChunkQueueingSuspended)
		return _FlushPendingData();

	fPendingDataBuffer = fChunkCompressor->QueueChunk(fPendingDataBuffer);
	fPendingDataSize = 0;

	if (!fChunkCompressor->IsFillingBatchFull())
		return B_OK;

	// Write the previously submitted batch, so we can submit the full one.
	status_t error = _WriteCompressedBatch();
	if (error != B_OK)
		return error;

	fChunkCompressor->SubmitFillingBatch();
	return B_OK;
}


status_t
PackageFileHeapWriter::_FlushPendingData()
{
	// queued chunks precede the pending data
	status_t error = _DrainCompressionQueue();
	if (error != B_OK)
		return error;

	if (fPendingDataSize == 0)
		return B_OK;

	error = _WriteChunk(fPendingDataBuffer, fPendingDataSize, true);
	if (error == B_OK)
		fPendingDataSize = 0;

	return error;
}


status_t
PackageFileHeapWriter::_WriteCompressedBatch()
{
	ChunkCompressor::Batch* batch = fChunkCompressor->CollectSubmittedBatch();
	if (batch == NULL)
		return B_OK;

	int32 count = batch->count;
	batch->count = 0;

	for (int32 i = 0; i < count; i++) {
		const ChunkCompressor::Job& job = batch->jobs[i];

		if (!fOffsets.Add(fCompressedHeapSize)) {
			fErrorOutput->PrintError("Out of memory!\n");
			return B_NO_MEMORY;
		}

		status_t error;
		if (job.error == B_OK && job.compressedSize < kChunkSize) {
			error = _WriteDataUncompressed(job.compressedData,
				job.compressedSize);
		} else if (job.error == B_OK || job.error == B_BUFFER_OVERFLOW) {
			// only use compressed data when we've actually saved space
			error = _WriteDataUncompressed(job.data, kChunkSize);
		} else {
			fErrorOutput->PrintError("Failed to compress chunk data: %s\n",
				strerror(job.error));
			error = job.error;
		}

		if (error != B_OK)
			return error;
	}

	return B_OK;
}


status_t
PackageFileHeapWriter::_DrainCompressionQueue()
{
	if (fChunkCompressor == NULL || !fChunkCompressor->HasQueuedChunks())
		return B_OK;

	status_t error = _WriteCompressedBatch();
	if (error != B_OK)
		return error;

	if (!fChunkCompressor->HasQueuedChunks())
		return B_OK;

	fChunkCompressor->SubmitFillingBatch();
	return _WriteCompressedBatch();
}


status_t
PackageFileHeapWriter::_WriteChunk(const void* data, size_t size,
	bool mayCompress)
{
	// add offset
	if (!fOffsets.Add(fCompressedHeapSize)) {
		fErrorOutput->PrintError("Out of memory!\n");
		return B_NO_MEMORY;
	}

	// Try to use compression only for data large enough.
	bool compress = mayCompress && size >= (off_t)kCompressionSizeThreshold;
	if (compress) {
		status_t error = _WriteDataCompressed(data, size);
		if (error != B_OK) {
			if (error != B_BUFFER_OVERFLOW)
				return error;
			compress = false;
		}
	}

	// Write uncompressed, if necessary.
	if (!compress) {
		status_t error = _WriteDataUncompressed(data, size);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


status_t
PackageFileHeapWriter::_WriteDataCompressed(const void* data, size_t size)
{
	if (fCompressionAlgorithm == NULL)
		return B_BUFFER_OVERFLOW;

	const iovec uncompressed = { (void*)data, size };
	iovec compressed = { fCompressedDataBuffer, size };
	status_t error = fCompressionAlgorithm->algorithm->CompressBuffer(
		uncompressed, compressed,
		fCompressionAlgorithm->parameters);
	if (error != B_OK) {
		if (error != B_BUFFER_OVERFLOW) {
			fErrorOutput->PrintError("Failed to compress chunk data: %s\n",
				strerror(error));
		}
		return error;
	}

	// only use compressed data when we've actually saved space
	if (compressed.iov_len == size)
		return B_BUFFER_OVERFLOW;

	return _WriteDataUncompressed(fCompressedDataBuffer, compressed.iov_len);
}


status_t
PackageFileHeapWriter::_WriteDataUncompressed(const void* data, size_t size)
{
	status_t error = fFile->WriteAtExactly(
		fHeapOffset + (off_t)fCompressedHeapSize, data, size);
	if (error != B_OK) {
		fErrorOutput->PrintError("Failed to write data: %s\n", strerror(error));
		return error;
	}

	fCompressedHeapSize += size;

	return B_OK;
}


void
PackageFileHeapWriter::_PushChunks(ChunkBuffer& chunkBuffer, uint64 startOffset,
	uint64 endOffset)
{
	if (endOffset > fUncompressedHeapSize) {
		fErrorOutput->PrintError("Invalid range to remove from heap\n");
		throw status_t(B_BAD_VALUE);
	}

	ssize_t chunkIndex = startOffset / kChunkSize;
	uint64 uncompressedChunkOffset = (uint64)chunkIndex * kChunkSize;

	while (startOffset < endOffset) {
		bool isLastChunk = fUncompressedHeapSize - uncompressedChunkOffset
			<= kChunkSize;
		uint32 inChunkOffset = uint32(startOffset - uncompressedChunkOffset);
		uint32 uncompressedChunkSize = isLastChunk
			? fUncompressedHeapSize - uncompressedChunkOffset
			: kChunkSize;
		uint64 compressedChunkOffset = fOffsets[chunkIndex];
		uint32 compressedChunkSize = isLastChunk
			? fCompressedHeapSize - compressedChunkOffset
			: fOffsets[chunkIndex + 1] - compressedChunkOffset;
		uint32 toKeepSize = uint32(std::min(
			(uint64)uncompressedChunkSize - inChunkOffset,
			endOffset - startOffset));

		if (!chunkBuffer.PushChunkSegment(compressedChunkOffset,
				compressedChunkSize, uncompressedChunkSize, inChunkOffset,
				toKeepSize)) {
			throw std::bad_alloc();
		}

		startOffset += toKeepSize;
		chunkIndex++;
		uncompressedChunkOffset += uncompressedChunkSize;
	}
}


void
PackageFileHeapWriter::_UnwriteLastPartialChunk()
{
	// If the last chunk is partial, read it in and remove it from the offsets.
	size_t lastChunkSize = fUncompressedHeapSize % kChunkSize;
	if (lastChunkSize != 0) {
		uint64 lastChunkOffset = fOffsets[fOffsets.Count() - 1];
		size_t compressedSize = fCompressedHeapSize - lastChunkOffset;

		status_t error = ReadAndDecompressChunkData(lastChunkOffset,
			compressedSize, lastChunkSize, fCompressedDataBuffer,
			fPendingDataBuffer);
		if (error != B_OK)
			throw error;

		fPendingDataSize = lastChunkSize;
		fCompressedHeapSize = lastChunkOffset;
		fOffsets.Remove(fOffsets.Count() - 1);
	}
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
