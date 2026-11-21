# Auswertung: Nachhall an Pro‑R 2 angepasst (alle 52 Aufnahmen)

Stand: 20.11.2026 · Aufnahmen: nicht öffentlich · Messwerte von Pro‑R: [`../pro-r-messwerte.csv`](../pro-r-messwerte.csv)

> **Weiter geht es in der [Auswertung 2 vom 21.11.2026](2026-11-21_Feinstruktur.md)** (Klangfarbe ohne Kammfilter, Reflexions‑Zug, Körnung, Pegel aller Regler). Die Zahlen hier sind der Stand vom 20.11.

## Kurz

- Alle 52 Aufnahmen sind ausgewertet. Die Export‑Kette ist sauber (Kalibrierung: 0 Samples Versatz, laut/leise genau 40,00 dB).
- Die beiden Decay‑Rate‑EQ‑Aufnahmen sind mit **Q 1,000** gerechnet, so wie du sie aufgenommen hast (Messplan‑Code angepasst).
- Nachhall wurde in 15 Schritten an die Aufnahmen angepasst. Jeder Schritt rechnet Nachhall selbst durch und vergleicht mit Pro‑R.
- Ergebnis: Hallzeit, Pegel, Klarheit, Klangfarbe, Stereo, Modulation und Thickness liegen jetzt nah an Pro‑R.
- Der Anfang des Halls ist mit frühen Abgriffen im Hallkern nachgebildet (siehe Nachtrag unten). Größte Rest‑Unterschiede: Extremstellungen (Brightness −100 %, Distance 0 / 100 %) und die Feinstruktur der ersten 50 ms.

## Vorher / nachher

Mittlere Abweichung zu Pro‑R. „Vorher“ = Nachhall mit den Pro‑R‑Skalen, aber noch ohne Anpassung.

| Messgröße | vorher | nachher |
|---|---|---|
| Hallzeit 500 Hz – 2 kHz | 11,0 % | 1,4 % |
| Hallzeit je Oktave 125 Hz – 8 kHz | 14,3 % | 3,1 % |
| Pegel | 2,2 dB | 0,3 dB |
| Klarheit C50 | 3,1 dB | 0,5 dB |
| Klangbalance 63 Hz – 8 kHz | 1,3 dB | 1,1 dB |
| Übersprechen (Width) | 3,4 dB | 0,3 dB |
| Tonbreite 1 kHz (Character) | 9,9 Hz | 1,6 Hz |
| Thickness laut/leise | 9,4 dB | 0,5 dB |
| Styles: Hallzeit / Pegel / C50 | 19 % / 2,6 dB / 7,6 dB | 0,6 % / 0,1 dB / 1,0 dB |
| Schwerpunktzeit Ts | 24 % | 12 % |
| Frühe Abklingzeit, Mitten | 12 % | 7,7 % |
| Frühe Abklingzeit, alle Frequenzen | 16 % | 13 % |

(Endstand nach dem Nachtrag unten. Vor den frühen Abgriffen waren es 20 %, 13 % und 22 %.)

## Was die Aufnahmen über Pro‑R zeigen

- **Hallzeit:** gemessen 4–16 % länger als angezeigt (z. B. Hall 1,85 s → 2,0 s).
- **Höhen:** sterben mit der Zeit ab, unabhängig vom Raum (wie Luftdämpfung). Lange Räume klingen deshalb oben dunkler aus.
- **Decay Rate:** 25 % ergibt nur ×0,23, 400 % ×3,9. Kurze Zeiten bleiben fast gleich laut.
- **Erste Reflexion:** immer nach 6,7 ms (Vintage 13,7 ms), in jedem Raum gleich.
- **Brightness −100 %:** Höhen kürzer, Bass +7 dB, insgesamt 6 dB leiser, Klarheit +2 dB. **+100 %:** Höhen +5 dB, kaum länger.
- **Character:** 0 % ganz statisch, 50 % kaum Modulation (Tonbreite 2,3 Hz), 100 % Chorus (14,4 Hz). Bei genau 50 % sind die Reflexionen 2 dB lauter.
- **Distance:** 0 % sehr starke, sehr helle Reflexionen (C50 +3,9 dB), 100 % weich (−16 dB).
- **Thickness:** +100 % = Kompressor 2:1, −100 % = Expander (leise Signale werden leiser), 0 % = linear.
- **Width:** 50 % = starkes Übersprechen (−2,7 dB), 70 % = getrennte Kanäle, 120 % = breiter (Korrelation −0,5).
- **Decay‑Rate‑EQ:** Low Shelf 200 Hz wirkt bis 250 Hz (×0,82), High Shelf 6 kHz erreicht ×1,4 bei 8 kHz und ×1,6 bei 16 kHz.
- **Vintage:** kleine Räume klingen nur halb so lang wie angezeigt (0,75 s → 0,40 s). Der Nachhall ist oben heller als bei Modern, große Räume dämpfen die Höhen stark.
- **Plate:** viel frühe Energie (kleiner Plate C50 +2,4 dB).

## Was in Nachhall geändert wurde

- 11 Raummodelle an den Pro‑R‑Zeiten (6 Symbole + 5 Zwischenpunkte): Hallzeit je Oktave, Pegel, Reflexionen, Aufbau, Klangfarbe des Nachhalls.
- Zeitabhängige Höhendämpfung, Korrektur je Decay‑Rate‑Stufe, Pegelausgleich für Decay Rate und Width.
- Reglerkurven für Brightness, Character, Distance, Thickness, Width (je 5 Punkte), Decay‑Rate‑EQ‑Shelves, Vintage und Plate (je 3 Punkte).
- Vintage: Das starke Höhen‑Filter (9 kHz) ist weg. Pro‑R beschneidet Vintage nicht so stark.
- Thickness: Sättigung erst über 0 % (Pro‑R ist bei 0 % linear).
- Fehler behoben: Die Umrechnung alter Projekte rief neue Funktionen ohne Namensraum auf. Das Plugin ließ sich deshalb in den letzten beiden Zwischenständen nicht bauen.

## Was noch anders ist

- **Frühe Abklingzeit bei Extremstellungen:** Brightness −100 % +47 %, Distance 0 % +28 %, Distance 100 % −12 % (Mitten). Sonst meist wenige Prozent (siehe Nachtrag).
- **Über 12 kHz:** kleine Räume 10–30 % kürzer, Nachhall der Basis etwa 5 dB weniger Höhen.
- **Echodichte 20–50 ms:** Nachhall dichter.
- **Distance 0–25 %:** Reflexionen weniger hell als bei Pro‑R.
- **Kleiner Plate:** 4–5 dB weniger frühe Energie. **Großer Vintage:** Höhen klingen länger.
- **Character:** Die Modulation ist zufällig. Einzelne Messungen schwanken um ±2–3 Hz.

## Prüfung

- `NachhallReference match reference/pro-r-messwerte.csv`: **MATCH OK** (läuft in GitHub Actions). Die frühe Abklingzeit ist als bekannter Unterschied markiert und darf nicht schlechter werden; die Schwerpunktzeit ist wieder eine normale Prüfung.
- `NachhallBehaviourTests`: 32 von 32 Prüfungen bestanden, Sollwerte = gemessene Pro‑R‑Werte.
- `NachhallDspTests`: alle Prüfungen bestanden, Pegel aller 23 Raum‑/Style‑Punkte auf ±0,5 dB an Pro‑R.
- Host‑Test (VST3) und MIDI‑Learn‑Test bestanden.

## Nachtrag: Anfang des Halls (frühe Abgriffe)

Frage: Warum war die frühe Abklingzeit (EDT) bei Nachhall länger als bei Pro‑R?

- Nachgemessen je Oktave: Bei Pro‑R ist in den Mitten (250 Hz – 2 kHz) die frühe Abklingzeit **gleich** der Hallzeit (Verhältnis 0,97–1,09). Pro‑R hat dort also keine zweite Neigung.
- Bei Nachhall war sie 10–20 % **länger**. Grund: Der Nachhall braucht nach dem Einsatz einige Millisekunden, bis er voll da ist. In dieser Lücke fehlt Energie, der Abfall startet zu langsam.
- Lösung: In jeder der 16 Hall‑Leitungen ein zweiter, früherer Ausgang (13–62 % der Länge, ab 160 Hz). Der Nachhall ist sofort da, die Hallzeit bleibt gleich. Stärke je Raum abgeglichen (`fit earlytap`).
- Nebenwirkungen und Korrekturen:
  - Die Leitungen tragen viel Bass, der sich am Hauptausgang aufhebt, an den frühen Stellen aber nicht. Deshalb der Hochpass bei 160 Hz.
  - Klangfarbe je Raum neu (jetzt drei Filter: Bass, untere Mitten 300 Hz, Höhen).
  - Distance bekam eine Pegelkurve für den Nachhall (Pro‑R wird bei 100 % rund 2,5 dB leiser).
  - Thickness unter 0 %: weniger frühe Abgriffe, luftiger, wie bei Pro‑R (dort ist der Anfang bei −100 % sehr dünn).
  - Vintage nutzt die Abgriffe nicht (war schon genau abgeglichen).
  - Brightness, Distance, Thickness, Vintage, Plate, Decay‑Rate‑Pegel neu abgeglichen.

| Messgröße (Mittel) | ohne Abgriffe | mit Abgriffen |
|---|---|---|
| Frühe Abklingzeit, Mitten | 12,9 % | 7,7 % |
| Schwerpunktzeit Ts | 19,9 % | 12,1 % |
| Frühe Abklingzeit, alle Frequenzen | 21,5 % | 13,1 % |
| Klarheit C50 | 0,5 dB | 0,8 dB |
| Pegel | 0,3 dB | 0,2 dB |
| Thickness laut/leise | 0,5 dB | 0,2 dB |

Was bleibt:
- Brightness −100 %: Pro‑R fällt am Anfang viel schneller ab (frühe Abklingzeit 1,35 s bei 2,0 s Hallzeit). Nachhall nicht (+47 %). Mehr frühe Abgriffe helfen dort nicht: Sie machen den ganzen Nachhall lauter.
- Distance 0 % / 100 %: frühe Abklingzeit +28 % / −12 %, Klarheit 2–4 dB daneben.
- Die ersten 50 ms sind bei Nachhall dichter (glatter) als bei Pro‑R.

