# Nachhall vs. FabFilter Pro‑R 2 – Vergleich

Stand: 21. November 2026.

Grundlage:
- **52 Aufnahmen des echten Pro‑R 2** aus dem Messpaket ([reference/](../reference/ANLEITUNG.md)). Gemessen wurde nur, was hinten aus Pro‑R herauskommt.
- Die offizielle Pro‑R‑2‑Hilfe, die Produktseite und der Testbericht von Sound On Sound.

## Kurzfazit

- **Funktionen:** Fast alles, was Pro‑R 2 hat, hat Nachhall auch. Es fehlen nur Dinge, die in MASCHINE keine Rolle spielen (Surround/Atmos, Pro‑Tools‑Hardware, weitere Formate).
- **Regler:** gleiche Skalen und Startwerte wie Pro‑R 2.
  - Brightness und Thickness: −100 … +100 %
  - Width: bis 120 %
  - Space: 6 Raum‑Symbole
- **Klang:** Nachhall wurde an die 52 Aufnahmen angepasst. Hallzeit, Pegel, Klarheit, Klangfarbe, Stereo, Modulation, Thickness und die Körnung (Echodichte) liegen jetzt nah an Pro‑R (Tabelle unten).
- **Anfang des Halls:** Bei Pro‑R ist der Nachhall sofort da, fällt von Anfang an gerade ab und besteht in den ersten 100 ms aus vielen einzelnen Reflexionen. Nachhall bildet das nach (frühe Abgriffe im Hallkern, Zug aus 112 einzelnen Reflexionen). Die frühe Abklingzeit der Mitten liegt im Mittel 3,8 % neben Pro‑R.
- **Nicht identisch:** Nachhall nutzt einen eigenen Algorithmus. Die größten Rest‑Unterschiede liegen bei Extremstellungen (Distance 0 % / 100 %, Decay Rate 25 %, Brightness −100 %) und bei den kleinsten Räumen (Abschnitt 4).

## Wie nah? Vorher / nachher

Mittlere Abweichung zu Pro‑R über alle passenden Aufnahmen. „Vorher“ ist Nachhall mit den neuen Skalen, aber noch ohne Anpassung.

| Messgröße | vorher | jetzt | schlechteste Aufnahme jetzt |
|---|---|---|---|
| Hallzeit 500 Hz – 2 kHz | 11,0 % | **1,2 %** | 4,9 % (Brightness −100 %) |
| Hallzeit je Oktave 125 Hz – 8 kHz | 14,3 % | **2,3 %** | 14 % (Decay Rate 25 %) |
| Pegel | 2,2 dB | **0,1 dB** | 2,3 dB (Thickness +100 %, Impuls) |
| Klarheit C50 | 3,1 dB | **0,4 dB** | 2,0 dB (Raum 0,75 s) |
| Frühe Abklingzeit EDT, Mitten 500 Hz – 2 kHz | 12 % | **3,9 %** | 14 % (Distance 0 %) |
| Schwerpunktzeit Ts | 24 % | **5,1 %** | 18 % (Thickness −100 %) |
| Frühe Abklingzeit EDT, alle Frequenzen | 16 % | **6,0 %** | 24 % (Decay Rate 25 %) |
| Klangbalance 63 Hz – 8 kHz | 1,3 dB | **0,75 dB** | 3,2 dB (Brightness −100 %) |
| Übersprechen (Width) | 3,4 dB | **0,1 dB** | 0,1 dB |
| Tonbreite 1 kHz (Character) | 9,9 Hz | **0,8 Hz** | 1,6 Hz |
| Thickness laut/leise | 9,4 dB | **0,6 dB** | 1,2 dB |
| Styles: Hallzeit | 19 % | **0,5 %** | 1,2 % |
| Styles: Pegel | 2,6 dB | **0,0 dB** | 0,0 dB |
| Styles: Klarheit C50 | 7,6 dB | **0,3 dB** | 0,5 dB |
| Echodichte 20–200 ms (Körnung) | – | **0,06** | 0,23 (Brightness −100 %) |
| Helligkeit der ersten 30 ms | – | **2,0 %** | 17 % (Brightness −100 %) |

Die Schritte dazwischen stehen in den Auswertungen vom [20.11.](../reference/auswertung/2026-11-20_Anpassung.md) und [21.11.](../reference/auswertung/2026-11-21_Feinstruktur.md).

Die frühe Abklingzeit der Mitten (500 Hz – 1 kHz) ist laut ISO 3382 das Maß dafür, wie hallig etwas klingt.

So wird das geprüft: `NachhallReference match reference/pro-r-messwerte.csv` rechnet Nachhall mit allen 52 Einstellungen durch und vergleicht mit den Messwerten von Pro‑R. Das läuft bei jedem Build in GitHub Actions.

## 1. Funktionen

✓ = vorhanden · ≈ = vorhanden, aber etwas anders gelöst · ✗ = fehlt

| Pro‑R 2 | Nachhall | |
|---|---|---|
| Space: stufenlos, 200 ms – 10 s, **6 Raum‑Symbole** (0,20 / 0,75 / 1,85 / 3,20 / 5,20 / 10,0 s) | gleich: 11 Raummodelle an den Pro‑R‑Zeiten, 6 Symbole | ✓ |
| Decay Rate 25 – 400 %, Anzeige „Endzeit + Originalzeit × Rate“ | gleich | ✓ |
| Style: Modern / Vintage / Plate | gleich | ✓ |
| Predelay 0 – 500 ms, Sync Off/1/4/1/8/1/16/1/32, Offset 50 – 200 % | gleich | ✓ |
| Character und Distance 0 – 100 % (Start 30 % / 50 %) | gleich | ✓ |
| Brightness und Thickness −100 … +100 % (Start 0 %) | gleich | ✓ |
| Stereo Width 0 – 120 % (Start 70 %) | gleich | ✓ |
| Ducking (Bereich), Auto Gate (Haltezeit ziehen, Tempo‑Sync) | gleich | ✓ |
| Freeze: „ewiges“ Ausklingen, Klick / gedrückt halten; Distance + Thickness wirken nicht mehr, Character, Brightness, Ducking, Auto Gate schon | gleich. Space und Decay Rate wirken erst nach dem Lösen (dazu sagt die Hilfe nichts) | ✓ |
| Mix (Start 22,5 %) + Lock Mix (gilt auch für neue Instanzen) | gleich | ✓ |
| Decay‑Rate‑EQ: 6 Bänder, Glocke / Low Shelf / High Shelf / Notch | gleich, Shelves an Pro‑R gemessen | ✓ |
| Post‑EQ: 6 Bänder, Low/High Cut bis 96 dB/Okt., Stereo/L/R/Mid/Side, automatische Lautstärke | gleich | ✓ |
| Display: Kurve ziehen = neues Band, Doppelklick / Strg‑Klick, Ränder = Shelf/Cut, unten = Notch | gleich | ✓ |
| Bänder: Rechteck‑Auswahl, Strg/Cmd‑Klick, Shift‑Klick (Bereich), Gruppen‑Ziehen (relativ), Shift fein, Alt Achse, Mausrad/Strg‑Ziehen = Q, Alt‑Klick bypass, Strg+Alt‑Klick Form, Alt+Shift‑Klick Steilheit, Doppelklick Werte, Rechtsklick Menü | gleich | ✓ |
| Werte‑Panel am Band (Enable, Form, Stereo‑Platz, Löschen) | gleich | ✓ |
| Analyzer: Reverb+Post / Pre+Post / Aus, Abkling‑Darstellung, Ausgangs‑Pegelanzeige | gleich | ✓ |
| Klaviatur: Punkte rasten auf Noten ein, Notennamen, Eingabe „A4“, „C#2+13“ | gleich | ✓ |
| Display‑Bereich ±30 / 18 / 9 dB | gleich | ✓ |
| Regler: Ziehen (temposensitiv), Shift fein, Strg/Cmd‑Klick = Standard, Doppelklick = Wert eintippen („1k“, „A4“, „2x“, „50%“), Mausrad | gleich | ✓ |
| Regler mit Alt koppeln („linked knobs“) | – | ✗ |
| Preset‑Browser: Ordner, Suche (Name/Ordner/Tags), Favoriten, Tags, Autor, Beschreibung, Tastatur (Pfeile, Enter, [ ]), Vor/Zurück | gleich | ✓ |
| „Default Setting“ wird beim Start geladen, „Save As Default“ | gleich | ✓ |
| Preset‑Name wird bei Änderungen abgedunkelt | abgedunkelt + „*“ | ✓ |
| IR‑Import (WAV/AIFF, Drag & Drop, Warnung bei Sweeps, Style = Modern) | gleich | ✓ |
| Undo / Redo, A/B, Kopieren | gleich | ✓ |
| MIDI Learn: Knopf, Klick + Controller bewegen, Enable MIDI, Clear (einzeln/alle), Revert, Save, automatisch gespeichert | gleich | ✓ |
| Vollbild (Esc beendet), Größen Medium / Large / Extra Large, Fenster frei ziehbar | gleich | ✓ |
| Skalierungs‑Untermenü (z. B. 150 %) | Größen + freies Ziehen, Betriebssystem‑Skalierung automatisch | ≈ |
| Ein/Aus‑Pegel + Panorama, weicher Bypass, 0 Latenz | gleich | ✓ |
| „Smart Parameter Interpolation“, sample‑genaue Automation | weiches Nachführen aller Regler in 0,7‑ms‑Schritten | ≈ |
| Surround / Dolby Atmos bis 9.1.6 | nur Stereo (MASCHINE ist Stereo) | ✗ |
| Formate VST2, CLAP, AAX, AudioSuite, iPad | VST3 + Standalone (Windows; AU beim Selbstbauen auf dem Mac) | ≈ |
| Pro‑Tools‑Bedienoberflächen | – | ✗ |

## 2. Was an Pro‑R angepasst wurde

Alle Werte stehen in `Source/dsp/RoomModels.h` (Räume) und `Source/dsp/Tuning.h` (Reglerkurven). Ermittelt mit `NachhallReference fit …`.

| Bereich | Was Pro‑R macht (gemessen) | Was Nachhall jetzt macht |
|---|---|---|
| Hallzeit | klingt 4–16 % länger als angezeigt | 11 Raummodelle: Hallzeit je Oktave meist auf ±5 % |
| Anfang des Halls | Nachhall ist sofort voll da, Abfall von Anfang an gerade (frühe = späte Abklingzeit in den Mitten) | frühe Abgriffe in allen 16 Hall‑Leitungen (13–62 % der Länge), Stärke je Raum; jede Leitung bekommt den Eingang zu ihrer eigenen Zeit (kein Kammfilter) |
| Reflexionen | wenige starke bis 20 ms, dann etwa eine einzelne Reflexion pro Millisekunde bis 100 ms, in großen Räumen identisch | 4 starke + Zug aus 112 einzelnen Reflexionen bis 100 ms (eigene Zufallszeiten), ganze Samples, Helligkeit je Raum |
| Körnung | wird langsam dicht (Halle bei 100–200 ms erst 0,54) | Länge der Hall‑Leitungen je Raum an die Echodichte angepasst (Halle ×1,5, kleine Räume ×2–3) |
| Höhen | Höhen sterben mit der Zeit ab, wie Luftdämpfung | eigene zeitabhängige Höhendämpfung |
| Decay Rate | 25 %: nur etwa ×0,23; kurze Zeiten bleiben laut und klingen heller | Korrektur je Stufe, Pegel‑ und Höhen‑Ausgleich |
| Erste Reflexion | immer nach 6,7 ms (Vintage 13,7 ms) | gleich |
| Klarheit / Aufbau | je Raum eigenes Verhältnis von Reflexionen zu Hall | je Raum angepasst (C50 auf ±0,5 dB) |
| Klangfarbe | gleichmäßig, oben herum heller als bei Nachhall vorher | vier kleine Filter nur für den Nachhall, je Raum; abgeglichen nach Klang je Oktave und Helligkeit des späten Halls |
| Brightness | −100 %: Höhen kürzer, Bass +7 dB, 6 dB leiser, klarer; die Reflexionen werden weniger hell als der Nachhall | Kurven an 5 Punkten angepasst, eigenes Höhen‑Filter für die Reflexionen |
| Character | 50 %: kaum Modulation (2,3 Hz), 100 %: Chorus (14,4 Hz); außer bei genau 50 % sind die Reflexionen dunkler | Tiefe und Tempo angepasst (Mittel über 8 Durchläufe), Reflexionen außerhalb 50 % dunkler und leiser |
| Distance | 0 %: sehr starke, helle Reflexionen; 100 %: weich, dichter, 2,5 dB leiser | Reflexions‑Pegel, Helligkeit, Aufbau, Streuung und Hall‑Pegel an 5 Punkten |
| Thickness | +100 %: Kompressor 2:1; −100 %: Expander, sehr dünner Anfang; +100 %: dicht; 0 %: linear | gleich, auf ±1,2 dB; Sättigung erst über 0 %; unter 0 % weniger Reflexionen (dünn), darüber zusätzliche (dicht) |
| Stereo Width | 50 %: −2,7 dB Übersprechen; 70 %: getrennt; 120 %: breiter | Übersprechen, Korrelation und Pegel angepasst |
| Decay‑Rate‑EQ | Shelves wirken breiter als ein normaler Filter | Lage und Steilheit der Shelves angepasst |
| Vintage | kleine Räume klingen nur halb so lang wie angezeigt; der Nachhall ist oben herum heller als bei Modern, große Räume dämpfen die Höhen stark | Hallzeit, Höhen, Pegel, Reflexionen und Klangfarbe bei 0,75 / 1,85 / 10 s; kein starkes Höhen‑Filter mehr |
| Plate | sehr viel frühe Energie, wenig ganz hohe Höhen | Hallzeit, Höhen, Pegel, frühe Energie und Klangfarbe bei 0,75 / 1,85 / 10 s |

## 3. Verhalten – gemessen

Jede Aussage der Pro‑R‑Hilfe ist eine Messung im Test `NachhallBehaviourTests` (läuft bei jedem Build automatisch). Wo Pro‑R gemessen wurde, vergleicht der Test mit den Pro‑R‑Werten.

| Pro‑R 2 sagt | Nachhall | Pro‑R 2 gemessen | |
|---|---|---|---|
| Space: 200 ms bis 10 s | gemessen 0,22 s … 10,9 s | 0,23 s … 10,8 s | ✓ |
| Decay Rate 25 – 400 % | ×0,23 … ×3,94 | ×0,23 … ×3,94 | ✓ |
| Brightness: weniger = mehr Höhen‑Absorption | 8 kHz ÷ 1 kHz: 0,43 / 0,79 / 0,86 | 0,45 / 0,77 / 0,85 | ✓ |
| Character 0 %: transparent | zeitlich völlig konstant | gleich | ✓ |
| Character 100 %: chorusartig | Tonbreite 15,4 Hz, 56 % verschmiert (Mittel über 8 Durchläufe: 14,4 Hz) | 14,4 Hz, 49 % | ✓ |
| Distance 0 % nah, 100 % weit | C50 +5,3 / −5,9 / −15,2 dB; Helligkeit 0–30 ms bei 0 %: 9,5 kHz | +3,9 / −6,2 / −16,0 dB; 9,5 kHz | ✓ |
| Thickness: wenig = luftig, viel = dicht | Echodichte 20–50 ms: 0,07 → 0,42 | 0,06 → 0,47 | ✓ |
| Thickness: dicht, nichtlinear | laut/leise: +12,5 / 0,0 / −15,8 dB | +13,4 / 0 / −14,9 dB | ✓ |
| Stereo Width | links→rechts 50 %: −2,6 dB, 70 %: −6,8 dB, 120 %: −8,8 dB | −2,7 / −6,9 / −8,9 dB | ✓ |
| Predelay | 100 ms → Hall beginnt nach 106,7 ms | erste Reflexion nach 6,7 ms | ✓ |
| Auto Gate mit Haltezeit | Haltezeit 100 ms → nach 0,61 s 50 dB leiser (ohne Gate 1,83 s) | nicht gemessen | ✓ |
| Freeze | 30 s: Pegel +0,1 dB, Höhen ±0,0 dB; Distance/Thickness wirkungslos | nicht gemessen | ✓ |
| Decay‑Rate‑EQ | High Shelf ×2: 8 kHz ×1,35, 16 kHz ×1,81; Low Shelf ×0,5: 125 Hz ×0,60, 250 Hz ×0,80 | ×1,40 / ×1,62; ×0,58 / ×0,82 | ✓ |
| Post‑EQ inkl. 96 dB/Okt., Mid/Side, Auto Gain | Glocke +6 → +5,9 dB; Low Cut 400 Hz → 200 Hz −93 dB | nicht gemessen | ✓ |
| Mix: 100 % für Sends | 0 % = Original bitgenau, 0 Latenz | – | ✓ |
| Pegel aller Räume und Styles | 17 Punkte auf ±0,1 dB | gemessen | ✓ |
| Vintage: klassischer Digitalhall | Nachhall über 12 kHz: Modern −16,3 dB, Vintage −15,9 dB (heller) | −16,8 / −12,4 dB (heller) | ✓ |
| Plate: schneller, dichter Anstieg | Echodichte 4–20 ms: Modern 0,13, Plate 0,55 | – | ✓ |
| „Hält in Mono gut“ (Sound On Sound) | Mono‑Summe −3,2 dB (keine Auslöschung) | – | ✓ |

## 4. Was noch anders ist

- **Distance 0 % / 100 %.** Bei 0 % ist Nachhall 0,8 dB klarer und die frühe Abklingzeit 13 % länger. Bei 100 % baut Pro‑R sehr langsam auf (Schwerpunktzeit 230 ms, Nachhall 199 ms).
- **Decay Rate 25 %.** Nachhall fällt bei hohen Frequenzen am Anfang schneller ab als Pro‑R (frühe Abklingzeit aller Frequenzen 0,53 s statt 0,71 s). Die Helligkeit des späten Halls passt (eigener Höhen‑Ausgleich je Decay‑Rate‑Stufe).
- **Kleinste Räume (0,4 und 0,75 s):** Klarheit etwa 2 dB zu niedrig. Pro‑R hat dort eine einzelne sehr starke Reflexion um 28 ms. Nachhall nutzt bewusst ein eigenes Zufallsmuster und kopiert die Pro‑R‑Zeiten nicht. Lautere erste Reflexionen würden die Klarheit treffen, machen dort aber frühe Abklingzeit und Schwerpunktzeit 15–20 % zu kurz; deshalb nur in den mittleren und großen Räumen genutzt.
- **Thickness ±100 %:** Pro‑R regelt einen einzelnen Impuls etwas anders als Dauersignale. Nachhall trifft die Dauersignale (±1,2 dB), ein Impuls ist bei +100 % 2,3 dB leiser als bei Pro‑R.
- **Brightness:** Bei −100 % ist der Anfang weniger dicht als bei Pro‑R (Echodichte 20–50 ms 0,41 statt 0,65) und 0,8 dB klarer. Bei +50/+100 % wird der späte Hall ×1,9 heller, bei Pro‑R ×2,4.
- **Großer Vintage‑Raum (10 s):** Anfang dünner als bei Pro‑R (Echodichte 20–50 ms 0,15 statt 0,39).
- **Modulation ist zufällig.** Einzelne Messungen schwanken um ±1–2 Hz. Verglichen wird der Mittelwert.
- **Nicht gemessen:** Freeze, Ducking, Auto Gate, Post‑EQ und Predelay‑Sync. Sie verhalten sich wie in der Hilfe beschrieben, Zahlen von Pro‑R gibt es dafür nicht.

## 5. Grenzen dieser Prüfung

- Gemessen wurde nur der Ausgang von Pro‑R, mit zwei Testsignalen. Zwischen den Messpunkten (z. B. Brightness −50 / 0 / +50 %) wird interpoliert.
- Alle Messungen sind bei 48 kHz und mit der Basis‑Einstellung gemacht (Hall 1,85 s). Andere Kombinationen sind nicht einzeln geprüft.
- Die Grenzwerte im Vergleich habe ich festgelegt. Bestanden heißt: 90 % der Aufnahmen liegen innerhalb der Grenze, die schlechteste innerhalb des 2,5‑Fachen. Die frühe Abklingzeit der Mitten ist wieder eine normale Prüfung (Grenze 10 %, erreicht 9 % bei 90 % der Aufnahmen). Nur die frühe Abklingzeit über alle Frequenzen ist noch als bekannter Unterschied markiert (Grenze 10 %, erreicht 12 %; die Prüfung schlägt an, sobald sie über 16 % steigt).
- Neu geprüft: Echodichte 20–200 ms (Grenze 0,2) und Helligkeit der ersten 30 ms (Grenze 15 %).
- Der Host‑Test mit geöffnetem Fenster läuft hier unter Linux nicht (Fehler des virtuellen Bildschirms beim Test‑Fenster). Das Plugin‑Fenster selbst wird erzeugt. pluginval prüft das Fenster in GitHub Actions auf Windows.

## 6. Selbst mit den Ohren vergleichen

1. FabFilter bietet Pro‑R 2 als kostenlose Testversion an.
2. In MASCHINE beide Plugins auf zwei Sends legen, Mix jeweils 100 %.
3. Gleiche Einstellungen wählen (Space‑Symbol, Decay Rate, Brightness, Character, Distance usw.) und umschalten.
4. Hallzeit, Klangfarbe, Pegel, der Anfang des Halls und seine Körnung sollten in der Grundstellung sehr ähnlich sein. Unterschiede am ehesten bei Extremstellungen und in den kleinsten Räumen (Abschnitt 4).
