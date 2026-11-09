# MorseOS

Emulatore di MorseOS: fa girare su PC il codice vero del firmware, con la band a schermo, il
quadrante e il tasto touch. Serve a scrivere e vedere una funzione prima di portarla sui dispositivi.

```shell
sudo apt install build-essential python3 libgtk-3-dev   # se mancano
make run
```

## Comandi

- **Tasto touch**: tieni premuto `SPAZIO`, oppure clicca il cerchio sotto lo schermo. Il tempo di
  pressione conta come sulla band.
- **Accesso al menu**: scrivi `TAN` (`- .- -.`), a mano o con il pulsante "Digita TAN".
- **Menu**: tocco breve (< 0,3 s) passa al riquadro successivo, tieni premuto 0,8 s per aprire.
- **Pannello laterale**: velocità Morse, testo digitato in automatico, ora, batteria, Bluetooth,
  notifiche e "trova la band". Il log mostra gli stessi eventi di `lt716 dumper touchlog`.

Opzioni: `--scale N`, `--lcd LxA`, `--no-time`, `--script FILE`, `--headless`.

## Come è fatto

La struttura è quella di `firmware/morseos` del repository `lt716`, con lo stesso `bundle.ini` e lo
stesso `tools/bundle.py`.

| Percorso | Origine |
|---|---|
| `apps/`, `core/ui/`, `lib/font`, `lib/morse`, `lib/rtc`, gli header di `lib/` | identici al firmware |
| `lib/display`, `lib/battery`, `lib/i2cprobe` (`*_emu.c`), `lib/fitpro` | sostituti dell'hardware |
| `core/system/system_emu.c` | il ciclo principale di `main.c`, senza radio |
| `emulator/` | orologio, finestra GTK, script, finti header dell'SDK Telink |

## Aggiungere una funzione

1. Crea `apps/<nome>/` con `<nome>.c` e `module.ini`, come le altre app.
2. Aggiungi `<nome> = y` in `[apps]` di `bundle.ini`. Il menu si costruisce da solo.
3. `make run` per provarla, `make test` per lo scenario automatico (`tests/smoke.emu`, schermate
   in `build/shots`).
4. Sul dispositivo copia la cartella dell'app e la riga di `bundle.ini`.

Se tieni l'app dentro l'interfaccia di `mos.h`, `font.h`, `st7735.h`, `morse.h` e `rtc.h`, il
codice si porta così com'è. Un nuovo sensore richiede una libreria reale e il suo sostituto `_emu`.

## Cosa non emula

Il fatto che una funzione giri qui **non** dice che sia sicura da installare: vedi
`INCIDENTE-0.5.5.md` nel repository `lt716`.

- Niente SDK Telink, radio, watchdog o gestione energetica: un blocco dentro una funzione di libreria
  come quello della 0.5.5 qui non si vede.
- L'orologio non resta indietro durante la sospensione, a differenza della band.
- Il pannello si aggiorna all'istante: la velocità SPI e gli eventuali lampi non sono riprodotti.
- Il sensore HRS3300 è un risultato fisso, e l'aggiornatore è solo una riga nel log.
