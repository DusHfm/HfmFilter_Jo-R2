# Messpaket: Pro‑R 2 aufnehmen, damit Nachhall näher herankommt

**Ziel:** Du schickst zwei Testsignale durch das echte Pro‑R 2 und nimmst das Ergebnis auf. Ich messe die Aufnahmen und passe Nachhall daran an.

**Aufwand:** 36 Pflicht‑Aufnahmen, etwa 1–2 Stunden. 18 weitere sind optional.

**Gemessen wird nur, was hinten aus Pro‑R herauskommt.** Nichts wird zerlegt oder kopiert.

## Stand (21.11.2026)

- **Alle 52 Aufnahmen sind da und ausgewertet.** Die zwei Post‑EQ‑Aufnahmen werden nicht gebraucht. Du musst nichts mehr aufnehmen.
- Nachhall ist an die Aufnahmen angepasst: Räume, alle Regler, Decay‑Rate‑EQ, Vintage und Plate ([Auswertung 1](auswertung/2026-11-20_Anpassung.md)), dazu Klangfarbe, Reflexionen und Körnung ([Auswertung 2](auswertung/2026-11-21_Feinstruktur.md)).
- Die Messwerte von Pro‑R liegen in [`pro-r-messwerte.csv`](pro-r-messwerte.csv). Bei jedem Build vergleicht GitHub Actions Nachhall damit.
- **Die Aufnahmen selbst sind nicht in diesem öffentlichen Repository** (es sind Aufnahmen eines Kaufprodukts). Hier liegen nur die daraus gemessenen Werte.

## Was du brauchst

- **Pro‑R 2, Testversion:** 30 Tage kostenlos ([fabfilter.com](https://www.fabfilter.com/products/pro-r-2-reverb)). Beim Start kommt jedes Mal ein Hinweis‑Fenster, das bestätigst du einfach.
- **MASCHINE** (oder eine DAW wie Reaper, Cubase, Ableton).
- **Die zwei Testsignale** aus dem Ordner [`signale/`](signale/). Datei öffnen → „Download raw file“.
  - **A** = `Test-A-Impuls.wav` (21 s): ein Klick links, ein Klick rechts.
  - **B** = `Test-B-Ton-Rauschen.wav` (29 s): Ton, lautes Rauschen, leises Rauschen, ein Klick.
- **Die Liste** [`Messplan.csv`](Messplan.csv) (öffnet sich in Excel). Unten steht sie auch als Tabelle.

## Schritt 1: Basis‑Einstellung in Pro‑R

Diese Einstellung gilt für **alle** Aufnahmen. In der Liste steht pro Aufnahme nur, was du davon abweichend änderst.

| Regler | Wert |
|---|---|
| Style | Modern |
| Space | **Raum‑Symbol 3** (1,85 s) |
| Decay Rate | 100 % |
| Predelay | 0 ms, Sync aus |
| Character | 50 % |
| Distance | 50 % |
| Brightness | **0 %** (die Mitte, Skala −100 … +100 %) |
| Thickness | **0 %** (die Mitte, Skala −100 … +100 %) |
| Stereo Width | 100 % |
| Mix | 100 % |
| Ducking | 0 (aus) |
| Auto Gate, Freeze | aus |
| Decay‑Rate‑EQ und Post‑EQ | keine Bänder (gerade Linie) |
| Ein‑/Ausgangspegel, Panorama | 0 dB, Mitte |

**Achtung:** Pro‑R startet mit anderen Werten, nämlich Character 30 %, Width 70 % und Mix 22,5 %.

Tipps:
- Genaue Werte eintippen: **Doppelklick** auf den Regler, z. B. `50%`.
- **Speichere die Basis als Preset** „Nachhall‑Basis“. Vor jeder Aufnahme lädst du dieses Preset und änderst nur das eine, was in der Liste steht. So schleichen sich keine Fehler ein.

## Schritt 2: Aufnahme vorbereiten (MASCHINE)

1. Neues Projekt, Tempo **120 BPM**. Audio auf **48 kHz** stellen (Preferences → Audio).
2. Testsignal A auf ein leeres Pad ziehen. Dadurch entsteht ein Sampler‑Sound.
3. Im Sampler die Hüllkurve auf **One‑shot** stellen. Kein Filter, keine Modulation, Tonhöhe 0.
4. Pattern‑Länge: **mindestens 11 Takte** für Signal A und **mindestens 15 Takte** für Signal B. Länger ist kein Problem, die Stille am Ende stört nicht (z. B. 16 Takte = 32 s). Eine Note auf den ersten Schritt, Velocity 127, Notenlänge über das ganze Pattern.
5. **Pro‑R als Effekt** auf diesen Sound laden (Slot hinter dem Sampler) und das Basis‑Preset laden.
6. Exportieren mit **Datei → Export Audio**:
   - Quelle Master
   - Bereich das ganze Pattern
   - 48 kHz, 24 Bit (oder 32 Bit)
   - **Normalisieren aus**, Loop Optimize aus
7. Die Datei so umbenennen, wie sie in der Liste heißt, z. B. `R05.wav`.

**Mit einer DAW (z. B. Reaper):** Testsignal auf eine Spur ganz an den Anfang legen und Pro‑R als Effekt auf diese Spur setzen. Dann die ganze Länge rendern: WAV, 48 kHz, 24 Bit, Normalisieren aus, Master ohne Limiter.

## Schritt 3: Jetzt als Nächstes

1. **`K_Kalibrierung_B.wav`**: Signal B mit Mix 0 %, alles andere wie Basis.
   - Bei der ersten B‑Aufnahme kam das laute Rauschen 9 dB leiser heraus als erwartet.
   - Diese Datei zeigt, ob das von Pro‑R kommt oder von der Kette (z. B. ein Limiter in MASCHINE).
2. **`B_Basis_A.wav` und `B_Basis_B.wav` neu**, mit der korrigierten Basis (Brightness 0 %, Thickness 0 %).
3. Diese 3 Dateien hochladen (Schritt 5).
4. Danach gleich mit den **A‑Aufnahmen** weitermachen (Räume, Styles, Decay Rate, Regler, Width). Ein Limiter in der Kette würde die Klick‑Aufnahmen kaum verändern.
5. Die übrigen **B‑Aufnahmen** (`C_…_B`, `T_…_B`) erst machen, wenn ich `K_Kalibrierung_B` geprüft habe.

## Schritt 4: Alle Aufnahmen laut Liste

- Jede Zeile = eine Aufnahme. Spalte **Testsignal** sagt, ob A oder B.
- **R01 … R06**: die 6 Raum‑Symbole um den Space‑Regler, von klein nach groß (0,20 / 0,75 / 1,85 / 3,20 / 5,20 / 10,0 s). Ein Klick auf das Symbol stellt den Raum ein.
- **RZ_…**: Space‑Zeiten zwischen den Symbolen (0,40 / 1,2 / 2,4 / 4,0 / 7,0 s). So stellst du sie ein:
  - Doppelklick auf den Space‑Regler und die Zeit eintippen.
  - Falls das nicht geht: drehen (mit Shift fein), bis die gelbe Zeit ungefähr passt.
- Bei jeder Raum‑Aufnahme die **gelbe Zeit** in die Spalte „Anzeige Hallzeit (s)“ eintragen. Den **Raumnamen** eintragen, falls Pro‑R einen anzeigt.
- **Brightness und Thickness** laufen von −100 bis +100 %. Die Liste nutzt −100, −50, +50 und +100 %.
- **Width** geht bis 120 %. Die Liste nutzt 0, 50, 70 und 120 %.
- Ein „x“ in die Spalte „erledigt“ hilft beim Überblick.

## Schritt 5: Aufnahmen ablegen

1. Die WAV‑Dateien lokal in den Ordner **`reference/pro-r/`** legen, dazu die ausgefüllte `Messplan.csv` und eine `INFO.txt` (Pro‑R‑Version, DAW, Export‑Format).
2. **Nicht in ein öffentliches Repository hochladen.** Der Ordner steht deshalb in `.gitignore`. Für Aufnahmen ein privates Repository nutzen.
3. Für den automatischen Vergleich in GitHub Actions reichen die Messwerte:

```
NachhallReference analyze reference/pro-r --csv reference/pro-r-messwerte.csv
```

## Was danach passiert

- Ich messe Pro‑R und Nachhall gleich:
  - Nachhallzeit je Frequenz
  - erste Reflexionen
  - Dichte und Aufbau
  - Stereo
  - Modulation
  - Klangfarbe und Pegel
- Ich passe Raummodelle und Reglerkurven an, bis die Werte zusammenpassen.
- Dazu kommt ein automatischer Vergleich. Jede Änderung zeigt dann, ob Nachhall näher an Pro‑R ist.

## Häufige Fehler

| Fehler | Folge | Lösung |
|---|---|---|
| Normalisieren an | Pegel falsch | aus |
| Limiter/Effekt auf dem Master | Klang verfälscht | alles andere aus |
| 44,1 kHz statt 48 kHz | Klick verschmiert | Audio auf 48 kHz |
| Export kürzer als das Testsignal | Hallfahne fehlt | Pattern mindestens 11 bzw. 15 Takte |
| Rest vom vorherigen Export im Pro‑R | Vorlauf nicht still | vor dem Export kurz Stopp/Play, notfalls Pro‑R neu laden |
| Pro‑R‑Startwerte statt Basis | falsche Werte | Basis‑Preset vor jeder Aufnahme laden |
| Brightness/Thickness auf 50 % statt 0 % | ganze Messreihe verschoben | 0 % ist die Mitte |

Das Auswerte‑Programm meldet die meisten dieser Fehler selbst.

## Die Liste

Signal A = `Test-A-Impuls.wav`, Signal B = `Test-B-Ton-Rauschen.wav`.

| Nr | Datei | Testsignal | Pflicht | Einstellung (Rest wie Basis) |
|---|---|---|---|---|
| 1 | `K_Kalibrierung.wav` | A | ja | Mix 0 % (prüft, ob der Export sauber ist) |
| 2 | `K_Kalibrierung_B.wav` | B | ja | Mix 0 % mit Signal B (prüft laut/leise durch die Kette) |
| 3 | `B_Basis_A.wav` | A | ja | Basis, nichts ändern |
| 4 | `B_Basis_B.wav` | B | ja | Basis, nichts ändern |
| 5 | `R01.wav` | A | ja | Space: Raum-Symbol 1 anklicken (0,20 s) |
| 6 | `RZ_0400ms.wav` | A | ja | Space auf 0,40 s stellen (zwischen Symbol 1 und 2) |
| 7 | `R02.wav` | A | ja | Space: Raum-Symbol 2 anklicken (0,75 s) |
| 8 | `RZ_1200ms.wav` | A | ja | Space auf 1,20 s stellen (zwischen Symbol 2 und 3) |
| 9 | `R03.wav` | A | ja | Space: Raum-Symbol 3 anklicken (1,85 s) |
| 10 | `RZ_2400ms.wav` | A | ja | Space auf 2,40 s stellen (zwischen Symbol 3 und 4) |
| 11 | `R04.wav` | A | ja | Space: Raum-Symbol 4 anklicken (3,20 s) |
| 12 | `RZ_4000ms.wav` | A | ja | Space auf 4,00 s stellen (zwischen Symbol 4 und 5) |
| 13 | `R05.wav` | A | ja | Space: Raum-Symbol 5 anklicken (5,20 s) |
| 14 | `RZ_7000ms.wav` | A | ja | Space auf 7,00 s stellen (zwischen Symbol 5 und 6) |
| 15 | `R06.wav` | A | ja | Space: Raum-Symbol 6 anklicken (10,0 s) |
| 16 | `S_Vintage_Klein.wav` | A | optional | Style Vintage + Raum-Symbol 2 (0,75 s) |
| 17 | `S_Vintage_Basis.wav` | A | ja | Style Vintage |
| 18 | `S_Vintage_Groesster.wav` | A | ja | Style Vintage + Raum-Symbol 6 (10,0 s) |
| 19 | `S_Plate_Klein.wav` | A | optional | Style Plate + Raum-Symbol 2 (0,75 s) |
| 20 | `S_Plate_Basis.wav` | A | ja | Style Plate |
| 21 | `S_Plate_Groesster.wav` | A | ja | Style Plate + Raum-Symbol 6 (10,0 s) |
| 22 | `D_DecayRate_025.wav` | A | ja | Decay Rate 25 % |
| 23 | `D_DecayRate_050.wav` | A | optional | Decay Rate 50 % |
| 24 | `D_DecayRate_200.wav` | A | optional | Decay Rate 200 % |
| 25 | `D_DecayRate_400.wav` | A | ja | Decay Rate 400 % |
| 26 | `H_Brightness_m100.wav` | A | ja | Brightness -100 % |
| 27 | `H_Brightness_m050.wav` | A | optional | Brightness -50 % |
| 28 | `H_Brightness_p050.wav` | A | optional | Brightness +50 % |
| 29 | `H_Brightness_p100.wav` | A | ja | Brightness +100 % |
| 30 | `C_Character_000_A.wav` | A | ja | Character 0 % |
| 31 | `C_Character_000_B.wav` | B | ja | Character 0 % |
| 32 | `C_Character_025_A.wav` | A | optional | Character 25 % |
| 33 | `C_Character_075_A.wav` | A | optional | Character 75 % |
| 34 | `C_Character_100_A.wav` | A | ja | Character 100 % |
| 35 | `C_Character_100_B.wav` | B | ja | Character 100 % |
| 36 | `E_Distance_000.wav` | A | ja | Distance 0 % |
| 37 | `E_Distance_025.wav` | A | optional | Distance 25 % |
| 38 | `E_Distance_075.wav` | A | optional | Distance 75 % |
| 39 | `E_Distance_100.wav` | A | ja | Distance 100 % |
| 40 | `T_Thickness_m100_A.wav` | A | ja | Thickness -100 % |
| 41 | `T_Thickness_m100_B.wav` | B | ja | Thickness -100 % |
| 42 | `T_Thickness_m050_A.wav` | A | optional | Thickness -50 % |
| 43 | `T_Thickness_p050_A.wav` | A | optional | Thickness +50 % |
| 44 | `T_Thickness_p100_A.wav` | A | ja | Thickness +100 % |
| 45 | `T_Thickness_p100_B.wav` | B | ja | Thickness +100 % |
| 46 | `W_Width_000.wav` | A | ja | Stereo Width 0 % |
| 47 | `W_Width_050.wav` | A | ja | Stereo Width 50 % |
| 48 | `W_Width_070.wav` | A | optional | Stereo Width 70 % (Standardwert) |
| 49 | `W_Width_120.wav` | A | ja | Stereo Width 120 % (Maximum) |
| 50 | `X_Predelay_100ms.wav` | A | optional | Predelay 100 ms (Sync aus) |
| 51 | `X_DecayEQ_HighShelf.wav` | A | optional | Decay-Rate-EQ: High Shelf, 6 kHz, Wert 200 %, Q 1 (Standard) |
| 52 | `X_DecayEQ_LowShelf.wav` | A | optional | Decay-Rate-EQ: Low Shelf, 200 Hz, Wert 50 %, Q 1 (Standard) |
| 53 | `X_PostEQ_Bell.wav` | A | optional | Character 0 % + Post-EQ: Bell, 1 kHz, +12 dB, Q 1 (Auto Gain aus) |
| 54 | `X_PostEQ_LowCut.wav` | A | optional | Character 0 % + Post-EQ: Low Cut, 400 Hz, 24 dB/Okt. (Auto Gain aus) |

## Für Entwickler: das Auswerte‑Programm

`NachhallReference` wird mit den Tests gebaut (`cmake --build build --target NachhallReference`).

```bash
NachhallReference signals reference/signale                      # Testsignale neu erzeugen
NachhallReference plan reference/Messplan.csv plan.md            # Messplan (CSV + Markdown)
NachhallReference nachhall /tmp/nachhall                         # alle Einträge durch Nachhall rendern
NachhallReference nachhall /tmp/n2 --extra "brightness=50"       # dasselbe mit zusätzlicher Einstellung
NachhallReference analyze reference/pro-r /tmp/nachhall --md reference/Vergleich.md
```

Die Codes in `Messplan.csv` benutzen die Einheiten von Pro‑R:
- Space in Sekunden
- alles andere in Prozent
- Brightness und Thickness −100 … +100 %

Nachhall zeigt dieselben Skalen wie Pro‑R. Intern entspricht 0 % bei Brightness und Thickness dem Wert 0,5.

`analyze` macht Folgendes:
- Aus `K_Kalibrierung` liest es Versatz und Pegel der Export‑Kette und rechnet beide heraus.
- Mit `K_Kalibrierung_B` prüft es, ob laut/leise unverändert durch die Kette kommt.
- Es meldet Probleme: Vorlauf nicht still, übersteuert, zu kurz, falsche Abtastrate.
- Es misst jede Aufnahme:
  - Nachhallzeit je Oktave (T30), EDT, C50, Schwerpunktzeit, Beginn, Echodichte (5–20, 20–50, 50–100, 100–200 ms), Pegel
  - Klangfarbe je Oktave, Übersprechen, Stereo‑Korrelation
  - mit Signal B zusätzlich:
    - Modulation (Ton‑Verschmierung, Tonbreite, zeitliche Änderung gesamt und bei 1 kHz)
    - Laut gegen leise (während und in der Fahne)

Anpassen und prüfen (Nachhall rechnet dabei selbst, Ergebnis = Zahlen zum Eintragen in
`Source/dsp/RoomModels.h` und `Source/dsp/Tuning.h`):

```
NachhallReference analyze reference/pro-r --csv reference/pro-r-messwerte.csv   # Messwerte von Pro-R
NachhallReference fit rooms reference/pro-r 3        # Hallzeit je Oktave + Pegel der 11 Räume
NachhallReference fit density reference/pro-r 0-10   # Raumgröße (Länge der Hall-Leitungen) nach der Echodichte
NachhallReference fit earlytap reference/pro-r 0-10  # frühe Abgriffe, Reflexionen, Aufbau: C50, Ts, EDT, Echodichte, Helligkeit
NachhallReference fit early reference/pro-r 2 0-3    # (älter) Erstreflexionen + Aufbau (Räume 0 bis 3)
NachhallReference fit roomsc50 reference/pro-r       # Klarheit C50 je Raum
NachhallReference fit primaryc50 reference/pro-r     # Klarheit C50 je Raum über die ersten starken Reflexionen
NachhallReference fit ercut reference/pro-r 0-10     # Helligkeit der ersten 30 ms je Raum
NachhallReference fit tone reference/pro-r 0-10      # Klangfarbe des Nachhalls je Raum
NachhallReference fit rate|brightness|width|character|thickness|thickc50|thickdens|distance|decayeq|charer reference/pro-r
NachhallReference fit styles reference/pro-r vintage # oder plate (Hallzeit, Höhen, Pegel, frühe Energie)
NachhallReference fit styletone reference/pro-r vintage  # oder plate (Klangfarbe je Style-Punkt)
NachhallReference match reference/pro-r-messwerte.csv  # Nachhall gegen Pro-R, läuft in GitHub Actions
```

`match` besteht, wenn bei jeder Messgröße 90 % der Aufnahmen innerhalb der Grenze liegen und die
schlechteste innerhalb des 2,5‑Fachen. Einzelne Aufnahmen treffen bekannte Unterschiede
(siehe [Auswertung](auswertung/2026-11-20_Anpassung.md)).
