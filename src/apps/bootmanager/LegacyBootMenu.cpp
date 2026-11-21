/*
 * Copyright 2008-2011, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Michael Pfeiffer <laplace@users.sourceforge.net>
 */


#include "LegacyBootMenu.h"

#include <new>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Catalog.h>
#include <DataIO.h>
#include <DiskDevice.h>
#include <DiskDeviceTypes.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <Drivers.h>
#include <File.h>
#include <Partition.h>
#include <Path.h>
#include <String.h>
#include <UTF8.h>

#include <fs_info.h>
#include <fs_interface.h>
#include <syscalls.h>

#include "BootDrive.h"
#include "BootLoader.h"


/*
	Note: for testing, the best way is to create a small file image, and
	register it, for example via the "diskimage" tool (mountvolume might also
	work).
	You can then create partitions on it via DriveSetup, or simply copy an
	existing drive to it, for example via:
		$ dd if=/dev/disk/ata/0/master/raw of=test.image bs=1M count=10

	It will automatically appear in BootManager once it is registered, and,
	depending on its parition layout, you can then install the boot menu on
	it, and directly test it via qemu.
*/


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LegacyBootMenu"


struct MasterBootRecord {
	uint8 bootLoader[440];
	uint8 diskSignature[4];
	uint8 reserved[2];
	uint8 partition[64];
	uint8 signature[2];
};


class LittleEndianMallocIO : public BMallocIO {
public:
			bool				WriteInt8(int8 value);
			bool				WriteInt16(int16 value);
			bool				WriteInt32(int32 value);
			bool				WriteInt64(int64 value);
			bool				WriteString(const char* value);
			bool				Align(int16 alignment);
			bool				Fill(int16 size, int8 fillByte);
};


class PartitionVisitor : public BDiskDeviceVisitor {
public:
								PartitionVisitor();

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

			bool				HasPartitions() const;
			off_t				FirstOffset() const;

private:
			off_t				fFirstOffset;
};


class PartitionRecorder : public BDiskDeviceVisitor {
public:
								PartitionRecorder(BMessage& settings,
									int8 biosDrive);

	virtual	bool				Visit(BDiskDevice* device);
	virtual	bool				Visit(BPartition* partition, int32 level);

			bool				FoundPartitions() const;

private:
			BMessage&			fSettings;
			int32				fUnnamedIndex;
			int8				fBIOSDrive;
			bool				fFound;
};


static const uint32 kBlockSize = 512;
static const uint32 kNumberOfBootLoaderBlocks = 4;
	// The number of blocks required to store the
	// MBR including the Haiku boot loader.

static const uint32 kMBRSignature = 0xAA55;

// Offset of the 32-bit little-endian "partition offset" field within a
// BFS boot block, as embedded by makebootable (see stage1.nasm's
// PARTITION_OFFSET_OFFSET/kPartitionOffset).
static const off_t kPartitionOffsetFieldOffset = 506;

// Temporary diagnostic logging for the "boot from file" feature, while
// it's still being debugged on real hardware. Appends to a fixed, known
// path so the user can read it back after a test. Remove once this
// feature is confirmed working end-to-end.
static void
_LogFileBootDebug(const char* format, ...)
{
	FILE* log = fopen("/boot/home/bootman-debug.log", "a");
	if (log == NULL)
		return;

	va_list args;
	va_start(args, format);
	vfprintf(log, format, args);
	va_end(args);

	fflush(log);
	fclose(log);
}

static const int32 kMaxBootMenuItemLength = 70;


bool
LittleEndianMallocIO::WriteInt8(int8 value)
{
	return Write(&value, sizeof(value)) == sizeof(value);
}


bool
LittleEndianMallocIO::WriteInt16(int16 value)
{
	return WriteInt8(value & 0xff)
		&& WriteInt8(value >> 8);
}


bool
LittleEndianMallocIO::WriteInt32(int32 value)
{
	return WriteInt8(value & 0xff)
		&& WriteInt8(value >> 8)
		&& WriteInt8(value >> 16)
		&& WriteInt8(value >> 24);
}


bool
LittleEndianMallocIO::WriteInt64(int64 value)
{
	return WriteInt32(value) && WriteInt32(value >> 32);
}


bool
LittleEndianMallocIO::WriteString(const char* value)
{
	int len = strlen(value) + 1;
	return WriteInt8(len)
		&& Write(value, len) == len;
}


bool
LittleEndianMallocIO::Align(int16 alignment)
{
	if ((Position() % alignment) == 0)
		return true;
	return Fill(alignment - (Position() % alignment), 0);
}


bool
LittleEndianMallocIO::Fill(int16 size, int8 fillByte)
{
	for (int i = 0; i < size; i ++) {
		if (!WriteInt8(fillByte))
			return false;
	}
	return true;
}


// #pragma mark -


PartitionVisitor::PartitionVisitor()
	:
	fFirstOffset(LONGLONG_MAX)
{
}


bool
PartitionVisitor::Visit(BDiskDevice* device)
{
	return false;
}


bool
PartitionVisitor::Visit(BPartition* partition, int32 level)
{
	if (partition->Offset() < fFirstOffset)
		fFirstOffset = partition->Offset();

	return false;
}


bool
PartitionVisitor::HasPartitions() const
{
	return fFirstOffset != LONGLONG_MAX;
}


off_t
PartitionVisitor::FirstOffset() const
{
	return fFirstOffset;
}


// #pragma mark -


PartitionRecorder::PartitionRecorder(BMessage& settings, int8 biosDrive)
	:
	fSettings(settings),
	fUnnamedIndex(0),
	fBIOSDrive(biosDrive),
	fFound(false)
{
}


bool
PartitionRecorder::Visit(BDiskDevice* device)
{
	return false;
}


bool
PartitionRecorder::Visit(BPartition* partition, int32 level)
{
	if (partition->ContainsPartitioningSystem())
		return false;

	BPath partitionPath;
	partition->GetPath(&partitionPath);

	BString buffer;
	BString name = partition->ContentName();
	if (name.Length() == 0) {
		BString number;
		number << ++fUnnamedIndex;
		buffer << B_TRANSLATE_COMMENT("Unnamed %d",
			"Default name of a partition whose name could not be read from "
			"disk; characters in codepage 437 are allowed only");
		buffer.ReplaceFirst("%d", number);
		name = buffer.String();
	}

	const char* type = partition->Type();
	if (type == NULL)
		type = B_TRANSLATE_COMMENT("Unknown", "Text is shown for an unknown "
			"partition type");

	BMessage message;
	// Data as required by BootLoader.h
	message.AddBool("show", true);
	message.AddString("name", name);
	message.AddString("type", type);
	message.AddString("path", partitionPath.Path());
	if (fBIOSDrive != 0)
		message.AddInt8("drive", fBIOSDrive);
	message.AddInt64("size", partition->Size());
	message.AddInt64("offset", partition->Offset());

	fSettings.AddMessage("partition", &message);
	fFound = true;

	return false;
}


bool
PartitionRecorder::FoundPartitions() const
{
	return fFound;
}


// #pragma mark -


LegacyBootMenu::LegacyBootMenu()
{
}


LegacyBootMenu::~LegacyBootMenu()
{
}


bool
LegacyBootMenu::IsInstalled(const BootDrive& drive)
{
	// TODO: detect bootman
	return false;
}


status_t
LegacyBootMenu::CanBeInstalled(const BootDrive& drive)
{
	BDiskDevice device;
	status_t status = drive.GetDiskDevice(device);
	if (status != B_OK)
		return status;

	PartitionVisitor visitor;
	device.VisitEachDescendant(&visitor);

	if (!visitor.HasPartitions()
			|| strcmp(device.ContentType(), kPartitionTypeIntel) != 0)
		return B_ENTRY_NOT_FOUND;

	// Enough space to write boot menu to drive?
	if (visitor.FirstOffset() < (int)sizeof(kBootLoader))
		return B_PARTITION_TOO_SMALL;

	if (device.IsReadOnlyMedia())
		return B_READ_ONLY_DEVICE;

	return B_OK;
}


status_t
LegacyBootMenu::CollectPartitions(const BootDrive& drive, BMessage& settings)
{
	status_t status = B_ERROR;

	// Remove previous partitions, if any
	settings.RemoveName("partition");

	BDiskDeviceRoster diskDeviceRoster;
	BDiskDevice device;
	bool partitionsFound = false;

	while (diskDeviceRoster.GetNextDevice(&device) == B_OK) {
		BPath path;
		status_t status = device.GetPath(&path);
		if (status != B_OK)
			continue;

		// Skip not from BIOS bootable drives that are not the target disk
		int8 biosDrive = 0;
		if (path != drive.Path()
			&& _GetBIOSDrive(path.Path(), biosDrive) != B_OK)
			continue;

		PartitionRecorder recorder(settings, biosDrive);
		device.VisitEachDescendant(&recorder);

		partitionsFound |= recorder.FoundPartitions();
	}

	return partitionsFound ? B_OK : status;
}


status_t
LegacyBootMenu::Install(const BootDrive& drive, BMessage& settings)
{
	int32 defaultPartitionIndex;
	if (settings.FindInt32("defaultPartition", &defaultPartitionIndex) != B_OK)
		return B_BAD_VALUE;

	// Optional: if present and >= 0, a "bootFile" entry is the default
	// instead of a "partition" entry.
	int32 defaultBootFileIndex = -1;
	settings.FindInt32("defaultBootFile", &defaultBootFileIndex);

	int32 timeout;
	if (settings.FindInt32("timeout", &timeout) != B_OK)
		return B_BAD_VALUE;

	int fd = open(drive.Path(), O_RDWR);
	if (fd < 0)
		return B_IO_ERROR;

	MasterBootRecord oldMBR;
	if (read(fd, &oldMBR, sizeof(oldMBR)) != sizeof(oldMBR)) {
		close(fd);
		return B_IO_ERROR;
	}

	if (!_IsValid(&oldMBR)) {
		close(fd);
		return B_BAD_VALUE;
	}

	LittleEndianMallocIO newBootLoader;
	ssize_t size = sizeof(kBootLoader);
	if (newBootLoader.Write(kBootLoader, size) != size) {
		close(fd);
		return B_NO_MEMORY;
	}

	MasterBootRecord* newMBR = (MasterBootRecord*)newBootLoader.Buffer();
	_CopyPartitionTable(newMBR, &oldMBR);

	int menuEntries = 0;
	int defaultMenuEntry = 0;
	BMessage partition;
	int32 index;
	for (index = 0; settings.FindMessage("partition", index,
			&partition) == B_OK; index ++) {
		bool show;
		partition.FindBool("show", &show);
		if (!show)
			continue;
		if (defaultBootFileIndex < 0 && index == defaultPartitionIndex)
			defaultMenuEntry = menuEntries;

		menuEntries ++;
	}

	BMessage file;
	for (index = 0; settings.FindMessage("bootFile", index, &file) == B_OK;
			index ++) {
		bool show;
		file.FindBool("show", &show);
		if (!show)
			continue;
		if (defaultBootFileIndex >= 0 && index == defaultBootFileIndex)
			defaultMenuEntry = menuEntries;

		menuEntries ++;
	}

	newBootLoader.WriteInt16(menuEntries);
	newBootLoader.WriteInt16(defaultMenuEntry);
	newBootLoader.WriteInt16(timeout);

	for (index = 0; settings.FindMessage("partition", index,
			&partition) == B_OK; index ++) {
		bool show;
		BString name;
		BString path;
		int64 offset;
		int8 drive;
		partition.FindBool("show", &show);
		partition.FindString("name", &name);
		partition.FindString("path", &path);
		// LegacyBootMenu specific data
		partition.FindInt64("offset", &offset);
		partition.FindInt8("drive", &drive);
		if (!show)
			continue;

		BString biosName;
		_ConvertToBIOSText(name.String(), biosName);

		newBootLoader.WriteString(biosName.String());
		newBootLoader.WriteInt8(drive);
		newBootLoader.WriteInt64(offset / kBlockSize);
	}

	for (index = 0; settings.FindMessage("bootFile", index, &file) == B_OK;
			index ++) {
		bool show;
		BString name;
		BString path;
		int64 partitionOffsetInFile;
		file.FindBool("show", &show);
		file.FindString("name", &name);
		file.FindString("path", &path);
		if (file.FindInt64("partitionOffset", &partitionOffsetInFile) != B_OK)
			partitionOffsetInFile = 0;
		if (!show)
			continue;

		int8 fileDrive;
		off_t fileOffsetBlocks;
		status_t status = _ResolveFileBootLocation(path.String(),
			partitionOffsetInFile, fileDrive, fileOffsetBlocks);
		if (status != B_OK) {
			close(fd);
			return status;
		}

		status = _PatchFileBootSectorOffset(path.String(),
			partitionOffsetInFile, fileOffsetBlocks);
		if (status != B_OK) {
			close(fd);
			return status;
		}

		BString biosName;
		_ConvertToBIOSText(name.String(), biosName);

		newBootLoader.WriteString(biosName.String());
		newBootLoader.WriteInt8(fileDrive);
		newBootLoader.WriteInt64(fileOffsetBlocks);
	}

	if (!newBootLoader.Align(kBlockSize)) {
		close(fd);
		return B_ERROR;
	}

	lseek(fd, 0, SEEK_SET);
	const uint8* buffer = (const uint8*)newBootLoader.Buffer();
	status_t status = _WriteBlocks(fd, buffer, newBootLoader.Position());
	close(fd);
	return status;
}


status_t
LegacyBootMenu::SaveMasterBootRecord(BMessage* settings, BFile* file)
{
	BString path;

	if (settings->FindString("disk", &path) != B_OK)
		return B_BAD_VALUE;

	int fd = open(path.String(), O_RDONLY);
	if (fd < 0)
		return B_IO_ERROR;

	ssize_t size = kBlockSize * kNumberOfBootLoaderBlocks;
	uint8* buffer = new(std::nothrow) uint8[size];
	if (buffer == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	status_t status = _ReadBlocks(fd, buffer, size);
	if (status != B_OK) {
		close(fd);
		delete[] buffer;
		return B_IO_ERROR;
	}

	MasterBootRecord* mbr = (MasterBootRecord*)buffer;
	if (!_IsValid(mbr)) {
		close(fd);
		delete[] buffer;
		return B_BAD_VALUE;
	}

	if (file->Write(buffer, size) != size)
		status = B_IO_ERROR;
	delete[] buffer;
	close(fd);
	return status;
}


status_t
LegacyBootMenu::RestoreMasterBootRecord(BMessage* settings, BFile* file)
{
	BString path;
	if (settings->FindString("disk", &path) != B_OK)
		return B_BAD_VALUE;

	int fd = open(path.String(), O_RDWR);
	if (fd < 0)
		return B_IO_ERROR;

	MasterBootRecord oldMBR;
	if (read(fd, &oldMBR, sizeof(oldMBR)) != sizeof(oldMBR)) {
		close(fd);
		return B_IO_ERROR;
	}
	if (!_IsValid(&oldMBR)) {
		close(fd);
		return B_BAD_VALUE;
	}

	lseek(fd, 0, SEEK_SET);

	size_t size = kBlockSize * kNumberOfBootLoaderBlocks;
	uint8* buffer = new(std::nothrow) uint8[size];
	if (buffer == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	if (file->Read(buffer, size) != (ssize_t)size) {
		close(fd);
		delete[] buffer;
		return B_IO_ERROR;
	}

	MasterBootRecord* newMBR = (MasterBootRecord*)buffer;
	if (!_IsValid(newMBR)) {
		close(fd);
		delete[] buffer;
		return B_BAD_VALUE;
	}

	_CopyPartitionTable(newMBR, &oldMBR);

	status_t status = _WriteBlocks(fd, buffer, size);
	delete[] buffer;
	close(fd);
	return status;
}


status_t
LegacyBootMenu::GetDisplayText(const char* text, BString& displayText)
{
	BString biosText;
	if (!_ConvertToBIOSText(text, biosText)) {
		displayText = "???";
		return B_ERROR;
	}

	// convert back to UTF-8
	int32 biosTextLength = biosText.Length();
	int32 bufferLength = strlen(text);
	char* buffer = displayText.LockBuffer(bufferLength + 1);
	int32 state = 0;
	if (convert_to_utf8(B_MS_DOS_CONVERSION,
		biosText.String(), &biosTextLength,
		buffer, &bufferLength, &state) != B_OK) {
		displayText.UnlockBuffer(0);
		displayText = "???";
		return B_ERROR;
	}

	buffer[bufferLength] = '\0';
	displayText.UnlockBuffer(bufferLength);
	return B_OK;
}


bool
LegacyBootMenu::_ConvertToBIOSText(const char* text, BString& biosText)
{
	// convert text in UTF-8 to 'code page 437'
	int32 textLength = strlen(text);

	int32 biosTextLength = textLength;
	char* buffer = biosText.LockBuffer(biosTextLength + 1);
	if (buffer == NULL) {
		biosText.UnlockBuffer(0);
		return false;
	}

	int32 state = 0;
	if (convert_from_utf8(B_MS_DOS_CONVERSION, text, &textLength,
		buffer, &biosTextLength, &state) != B_OK) {
		biosText.UnlockBuffer(0);
		return false;
	}

	buffer[biosTextLength] = '\0';
	biosText.UnlockBuffer(biosTextLength);
	return biosTextLength < kMaxBootMenuItemLength;
}


status_t
LegacyBootMenu::_GetBIOSDrive(const char* device, int8& drive)
{
	int fd = open(device, O_RDONLY);
	if (fd < 0)
		return errno;

	status_t status = ioctl(fd, B_GET_BIOS_DRIVE_ID, &drive, 1);
	close(fd);
	return status;
}


/*!	Resolves a "boot from file" entry's chainload target: the BIOS drive
	and absolute LBA (in disk blocks) of the actual bootable BFS partition
	on its host disk.

	\a partitionOffsetInFile is the byte offset, within the image file
	itself, where the bootable BFS partition begins. It is 0 only when the
	whole file was formatted directly as a single BFS volume; when the
	file was instead given its own inner partition table (e.g. "Intel")
	with the BFS partition created inside it -- the normal result of
	registering the file in DriveSetup and partitioning it like any other
	disk -- byte 0 of the file is that inner partition table, not a BFS
	boot block, and this offset must point past it to where the BFS
	partition (and its boot block, written there by makebootable) really
	starts.

	The file must be PHYSICALLY contiguous on its host filesystem -- the
	same constraint the old BeOS Personal Edition "boot from a file"
	feature had. This is verified here, not assumed, since the file's
	physical location can change between runs (it is never cached; this is
	called fresh on every Install()).

	Note this is checked across potentially many file_io_vec extents, not
	just one: BFS (and most filesystems) can only represent a run of
	physically contiguous blocks up to some maximum length per on-disk
	record (BFS's block_run::length field is 16 bits, capping a single
	record at MAX_BLOCK_RUN_LENGTH blocks -- a few tens to a few hundred
	MiB depending on block size). A multi-hundred-MiB disk image that is
	genuinely contiguous on disk will still be reported as several
	back-to-back extents for this reason, so requiring exactly one extent
	would reject nearly every realistically-sized image even when it is
	perfectly contiguous. What actually matters is that every extent
	picks up exactly where the previous one left off.
*/
status_t
LegacyBootMenu::_ResolveFileBootLocation(const char* path,
	off_t partitionOffsetInFile, int8& drive, off_t& offsetBlocks)
{
	_LogFileBootDebug("_ResolveFileBootLocation: path=\"%s\" "
		"partitionOffsetInFile=%" B_PRIdOFF "\n", path, partitionOffsetInFile);

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		_LogFileBootDebug("  open() failed: errno=%d (%s)\n", errno,
			strerror(errno));
		return errno;
	}

	struct stat fileStat;
	if (fstat(fd, &fileStat) != 0) {
		int savedErrno = errno;
		_LogFileBootDebug("  fstat() failed: errno=%d (%s)\n", savedErrno,
			strerror(savedErrno));
		close(fd);
		return savedErrno;
	}

	_LogFileBootDebug("  file size=%" B_PRIdOFF "\n", fileStat.st_size);

	if (partitionOffsetInFile < 0 || partitionOffsetInFile >= fileStat.st_size) {
		_LogFileBootDebug("  partitionOffsetInFile out of range -> "
			"B_BAD_VALUE\n");
		close(fd);
		return B_BAD_VALUE;
	}

	const size_t kMaxExtents = 1024;
	file_io_vec* vecs = new(std::nothrow) file_io_vec[kMaxExtents];
	if (vecs == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	size_t count = kMaxExtents;
	status_t status = _kern_get_file_map(fd, 0, fileStat.st_size, vecs,
		&count);
	close(fd);

	_LogFileBootDebug("  _kern_get_file_map: status=%" B_PRId32
		" (%s) count=%" B_PRIuSIZE "\n", status, strerror(status), count);
	for (size_t i = 0; i < count && i < 16; i++) {
		_LogFileBootDebug("    vec[%" B_PRIuSIZE "]: offset=%" B_PRIdOFF
			" length=%" B_PRIdOFF "\n", i, vecs[i].offset, vecs[i].length);
	}

	if (status != B_OK && status != B_BUFFER_OVERFLOW) {
		delete[] vecs;
		return status;
	}
	if (status == B_BUFFER_OVERFLOW) {
		// Too many extents to even check -- treat as unsupported, same as
		// genuinely fragmented.
		_LogFileBootDebug("  too many extents -> B_NOT_SUPPORTED\n");
		delete[] vecs;
		return B_NOT_SUPPORTED;
	}

	bool contiguous = true;
	for (size_t i = 1; i < count; i++) {
		if (vecs[i - 1].offset + vecs[i - 1].length != vecs[i].offset) {
			contiguous = false;
			break;
		}
	}

	off_t fileStartOffset = vecs[0].offset;
	delete[] vecs;

	if (!contiguous) {
		// The file is physically fragmented on its host filesystem -- it
		// cannot be chainloaded directly.
		_LogFileBootDebug("  extents not contiguous -> B_NOT_SUPPORTED\n");
		return B_NOT_SUPPORTED;
	}

	// BDiskDeviceRoster::GetPartitionForPath() needs a partition's raw
	// device path (e.g. "/dev/disk/.../N") -- it does a plain string
	// match against each registered partition's own GetPath(), it does
	// NOT resolve "the partition containing this filesystem path" the
	// way its name suggests. Resolve the actual device path hosting this
	// file first, the same way makebootable.cpp does for a directory
	// argument (fs_stat_dev() on the file's st_dev).
	fs_info hostInfo;
	status = fs_stat_dev(fileStat.st_dev, &hostInfo);
	_LogFileBootDebug("  fs_stat_dev: status=%" B_PRId32 " (%s) "
		"device_name=\"%s\"\n", status, strerror(status),
		status == B_OK ? hostInfo.device_name : "");
	if (status != B_OK)
		return status;

	BDiskDeviceRoster roster;
	BDiskDevice device;
	BPartition* partition;
	status = roster.GetPartitionForPath(hostInfo.device_name, &device,
		&partition);
	_LogFileBootDebug("  GetPartitionForPath: status=%" B_PRId32 " (%s)\n",
		status, strerror(status));
	if (status != B_OK)
		return status;

	_LogFileBootDebug("  host partition: Offset()=%" B_PRIdOFF
		" Size()=%" B_PRIdOFF " ContentName()=\"%s\"\n", partition->Offset(),
		partition->Size(), partition->ContentName());

	BPath devicePath;
	status = device.GetPath(&devicePath);
	_LogFileBootDebug("  host device.GetPath(): status=%" B_PRId32
		" (%s) path=\"%s\"\n", status, strerror(status), devicePath.Path());
	if (status != B_OK)
		return status;

	status = _GetBIOSDrive(devicePath.Path(), drive);
	_LogFileBootDebug("  _GetBIOSDrive: status=%" B_PRId32 " (%s) drive=%d\n",
		status, strerror(status), (int)drive);
	if (status != B_OK)
		return status;

	offsetBlocks = (partition->Offset() + fileStartOffset
		+ partitionOffsetInFile) / kBlockSize;

	_LogFileBootDebug("  RESULT: drive=%d offsetBlocks=%" B_PRIdOFF
		" (= (hostOffset %" B_PRIdOFF " + fileStartOffset %" B_PRIdOFF
		" + partitionOffsetInFile %" B_PRIdOFF ") / %" B_PRIu32 ")\n",
		(int)drive, offsetBlocks, partition->Offset(), fileStartOffset,
		partitionOffsetInFile, kBlockSize);

	return B_OK;
}


/*!	Patches the "partition offset" field embedded by makebootable in the
	image file's own boot block (byte kPartitionOffsetFieldOffset, 32-bit
	little endian -- see stage1.nasm's PARTITION_OFFSET_OFFSET/
	kPartitionOffset).

	makebootable bakes this field in relative to whatever device it was
	run against -- for the BFS partition inside a registered
	file-disk-device, that's an offset relative to the START OF THAT
	PARTITION within the virtual disk (effectively 0, since the field
	means "where does my own partition begin"), not an offset on the real
	physical disk the file happens to live on. stage1 unconditionally adds
	this value to every block number it computes once chainloaded (see
	readBlocks in stage1.nasm), so once this code is entered by jumping
	into the middle of a real disk, the baked-in value must be the BFS
	partition's real absolute disk offset -- otherwise stage1 reads
	whatever else happens to sit at that disk offset instead of this
	file's own BFS structures, silently booting something else entirely.
	Re-patched every time Install() runs, for the same reason the
	chainload LBA itself is never cached.

	\a partitionOffsetInFile locates the actual boot sector to patch: when
	the file has its own inner partition table, the boot sector written
	by makebootable lives at that offset within the file, not at the
	file's byte 0 -- see _ResolveFileBootLocation().
*/
status_t
LegacyBootMenu::_PatchFileBootSectorOffset(const char* path,
	off_t partitionOffsetInFile, off_t offsetBlocks)
{
	_LogFileBootDebug("_PatchFileBootSectorOffset: path=\"%s\" "
		"partitionOffsetInFile=%" B_PRIdOFF " offsetBlocks=%" B_PRIdOFF "\n",
		path, partitionOffsetInFile, offsetBlocks);

	int fd = open(path, O_RDWR);
	if (fd < 0) {
		_LogFileBootDebug("  open() failed: errno=%d (%s)\n", errno,
			strerror(errno));
		return errno;
	}

	// Sanity check: read back the boot sector signature at the very end
	// of the 512-byte sector we're about to patch, and the old value
	// already there, before writing anything.
	uint8 signature[2];
	pread(fd, signature, sizeof(signature), partitionOffsetInFile + 510);
	uint32 oldValue = 0;
	pread(fd, &oldValue, sizeof(oldValue),
		partitionOffsetInFile + kPartitionOffsetFieldOffset);
	_LogFileBootDebug("  before patch: signature=%02x%02x (expect aa55) "
		"oldValue=%" B_PRIu32 "\n", signature[1], signature[0], oldValue);

	uint32 value = (uint32)offsetBlocks;
	ssize_t written = pwrite(fd, &value, sizeof(value),
		partitionOffsetInFile + kPartitionOffsetFieldOffset);

	uint32 verifyValue = 0;
	pread(fd, &verifyValue, sizeof(verifyValue),
		partitionOffsetInFile + kPartitionOffsetFieldOffset);
	_LogFileBootDebug("  wrote %" B_PRIu32 " (written=%" B_PRIdSSIZE
		"), read back %" B_PRIu32 "\n", value, written, verifyValue);

	close(fd);

	if (written != (ssize_t)sizeof(value))
		return B_IO_ERROR;

	return B_OK;
}


status_t
LegacyBootMenu::_ReadBlocks(int fd, uint8* buffer, size_t size)
{
	if (size % kBlockSize != 0) {
		fprintf(stderr, "_ReadBlocks buffer size must be a multiple of %d\n",
			(int)kBlockSize);
		return B_BAD_VALUE;
	}
	const size_t blocks = size / kBlockSize;
	uint8* block = buffer;
	for (size_t i = 0; i < blocks; i ++, block += kBlockSize) {
		if (read(fd, block, kBlockSize) != (ssize_t)kBlockSize)
			return B_IO_ERROR;
	}
	return B_OK;
}


status_t
LegacyBootMenu::_WriteBlocks(int fd, const uint8* buffer, size_t size)
{
	if (size % kBlockSize != 0) {
		fprintf(stderr, "_WriteBlocks buffer size must be a multiple of %d\n",
			(int)kBlockSize);
		return B_BAD_VALUE;
	}
	const size_t blocks = size / kBlockSize;
	const uint8* block = buffer;
	for (size_t i = 0; i < blocks; i ++, block += kBlockSize) {
		if (write(fd, block, kBlockSize) != (ssize_t)kBlockSize)
			return B_IO_ERROR;
	}
	return B_OK;
}


void
LegacyBootMenu::_CopyPartitionTable(MasterBootRecord* destination,
		const MasterBootRecord* source)
{
	memcpy(destination->diskSignature, source->diskSignature,
		sizeof(source->diskSignature) + sizeof(source->reserved)
			+ sizeof(source->partition));
}


bool
LegacyBootMenu::_IsValid(const MasterBootRecord* mbr)
{
	return mbr->signature[0] == (kMBRSignature & 0xff)
		&& mbr->signature[1] == (kMBRSignature >> 8);
}
