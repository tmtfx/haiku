# Configure Progress Scripts

Script helper per visualizzare il progresso del `configure` di Haiku in tempo reale durante la compilazione dei cross-tools.

## 📋 Problemi risolti

- ✅ Il `configure` dura ore senza feedback visibile
- ✅ Non è possibile sapere se il processo è bloccato o progredisce
- ✅ Difficile stimare quanto tempo rimane
- ✅ Nessuna indicazione della fase corrente

## 🎯 Due soluzioni disponibili

### 1. `configure-with-progress` - Barra percentuale con ETA

Mostra una barra di progresso suddivisa in fasi principali con stima ETA.

**Caratteristiche:**
- Barra di progresso visiva (0-100%)
- Calcolo automatico ETA basato su tempo trascorso
- Elenco delle fasi che verranno eseguite
- Tempo totale alla fine
- Suggerimenti automatici per i prossimi comandi

**Utilizzo:**

```bash
cd generated.x86_64
../configure-with-progress \
  --cross-tools-source ../../buildtools \
  --build-cross-tools x86_64
```

Oppure con più architetture:

```bash
../configure-with-progress \
  --cross-tools-source ../../buildtools \
  --build-cross-tools x86_gcc2 \
  --build-cross-tools x86
```

**Output esempio:**
```
╔════════════════════════════════════════════════════════════════╗
║  Haiku Configure con Barra di Progresso
║  2026-10-29 21:27:57
╚════════════════════════════════════════════════════════════════╝

[████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░] 40% - Building cross-tools (x86_64)... [ETA: 1h 23m 45s]

✓ Configurazione completata con successo!
Tempo totale: 2h 05m 30s

Prossimo passo:
  cd generated.x86_64 && jam -q
```

---

### 2. `configure-live` - Monitoraggio live dettagliato

Monitoraggio live con barra animata e conteggio righe di log elaborate.

**Caratteristiche:**
- Barra animata che progredisce nel tempo
- Conta le righe di log elaborate
- Estrae automaticamente la fase corrente
- Mostra il log completo alla fine
- Ideale per debugging

**Utilizzo:**

```bash
cd generated.x86_64
../configure-live \
  --cross-tools-source ../../buildtools \
  --build-cross-tools x86_64
```

**Output esempio:**
```
╔════════════════════════════════════════════════════════════════╗
║  Haiku Configure - Monitoraggio Live
║  2026-10-29 21:27:57
╚════════════════════════════════════════════════════════════════╝

Architetture da compilare: 1

Progresso:

[▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░] 23m 15s
Fase: Building cross-tools (GCC 4) - x86_64
Righe log: 4521

✓ Configurazione completata con successo!
Tempo totale: 2h 05m 30s

Prossimi passi:
  cd generated.x86_64
  jam -q

Log completo disponibile in: /tmp/haiku_configure_12345.log
```

---

## 🔍 Quale scegliere?

| Situazione | Script consigliato |
|-----------|-------------------|
| Vuoi sapere quanto tempo rimane | `configure-with-progress` |
| Preferisci feedback costante | `configure-live` |
| Stai debuggando | `configure-live` (mostra log dettagliato) |
| Vuoi una stima accurata | `configure-with-progress` |
| Vuoi il massimo della chiarezza | `configure-with-progress` |

---

## 📊 File di log

Entrambi gli script salvano un log completo in `/tmp/haiku_configure_<PID>.log`:

```bash
# Per esaminare il log dopo (se qualcosa va storto)
tail -f /tmp/haiku_configure_12345.log

# Visualizza tutte le righe di output del configure
cat /tmp/haiku_configure_12345.log | less
```

---

## 🐛 Troubleshooting

### Script non trovato
Se gli script non sono eseguibili:
```bash
chmod +x configure-with-progress configure-live
```

### Percorsi relativi
Gli script usano percorsi relativi. Usa `../` dal directory `generated.x86_64`:

```bash
cd generated.x86_64
../configure-with-progress [opzioni]
```

### Non vedo output
Se lo script non mostra output:
```bash
# Controlla il log direttamente
tail -50 /tmp/haiku_configure_*.log
```

---

## 💡 Suggerimenti di utilizzo

### Per un build veloce di prova
```bash
# Senza compilare cross-tools (solo config)
../configure-live --host-only
```

### Per architetture multiple
```bash
# Compila sia x86_gcc2 che x86
../configure-live \
  --cross-tools-source ../../buildtools \
  --build-cross-tools x86_gcc2 \
  --build-cross-tools x86
```

### In background con nohup
```bash
# Se vuoi eseguire in background e staccare dal terminal
nohup ../configure-live \
  --cross-tools-source ../../buildtools \
  --build-cross-tools x86_64 > configure_output.txt 2>&1 &

# Poi controlla il progresso
tail -f configure_output.txt
```

---

## 📝 Note tecniche

### Come funzionano gli script

1. **configure-with-progress**: Divide il configure in 6 fasi stimate e calcola il progresso leggendo il log
2. **configure-live**: Monitora costantemente il file di log e mostra le statistiche aggiornate ogni secondo

### Parsing del log

Gli script estraggono il progresso cercando questi pattern nel log del configure:
- `build_cross_tools_gcc4` → fase di compilazione cross-tools
- `Configuring binutils` → compilazione binutils
- `Configuring gcc` → compilazione GCC
- `Generating BuildConfig` → finalizzazione

Se hai estensioni personalizzate al configure, potrebbe essere necessario aggiornare i pattern di ricerca negli script.

---

## 🔗 Comandi correlati

Dopo aver completato il configure:

```bash
# Compila il sistema
cd generated.x86_64
jam -q

# O con parallelizzazione
jam -j4

# Compila solo un target specifico (es. accelerante intel_arc)
jam -q intel_arc intel_arc.accelerant
```

---

**Creato per:** tmtfx/haiku fork  
**Data:** 2026-10-29  
**Versione:** 1.0
