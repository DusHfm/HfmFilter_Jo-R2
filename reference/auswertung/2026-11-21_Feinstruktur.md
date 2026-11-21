# Auswertung 2: Klangfarbe, Reflexionen und Körnung

Stand: 21.11.2026 · Grundlage: dieselben 52 Aufnahmen wie in der [ersten Auswertung](2026-11-20_Anpassung.md) · Messwerte von Pro‑R: [`../pro-r-messwerte.csv`](../pro-r-messwerte.csv)

## Kurz

- Hallzeit und Pegel passten schon. Jetzt ging es um das, was man trotzdem noch hört: Klangfarbe, die Helligkeit der Reflexionen und die **Körnung** (wie schnell der Hall von einzelnen Reflexionen zu glattem Rauschen wird).
- Drei echte Fehler gefunden und behoben (Kammfilter im Aufbau, dumpfe Reflexionen, zu kurzer Reflexions‑Zug).
- Danach alle Räume und Regler neu abgeglichen, dazu neue Abgleiche für Raumgröße (Körnung), Character‑Reflexionen, Vintage/Plate‑Klang, Thickness‑ und Distance‑Körnung.
- Zwei neue Prüfungen im Pro‑R‑Vergleich: Echodichte 20–200 ms und Helligkeit der ersten 30 ms.
- Nebenbei: Modern braucht nur noch etwa halb so viel Rechenzeit (gleiches Signal, Bit für Bit).

## Was die Aufnahmen zusätzlich zeigen

- **Pro‑R hat einen langen Zug einzelner Reflexionen.** Zwischen 20 und 100 ms kommt etwa eine Reflexion pro Millisekunde, alle ähnlich laut. In großen Räumen sind Zeiten und Pegel sogar identisch (R04, R05, R06).
- **Pro‑R wird langsamer dicht.** Die Echodichte (1 = glatt wie Rauschen) ist in der Halle bei 100–200 ms erst 0,54. Das passt zu längeren Hall‑Leitungen als bisher bei Nachhall: Halle etwa ×1,5, kleine Räume ×2–3.
- **Thickness ändert die Körnung stark:** −100 % sehr dünn (0,06 bei 20–50 ms), +100 % dicht (0,47). **Distance 100 %** ist dichter (0,65 bei 50–100 ms).
- **Character:** Nur bei genau 50 % sind die Reflexionen hell (Schwerpunkt 6,4 kHz). Bei 0 / 25 / 75 / 100 % sind sie dunkler (4,6–4,9 kHz) und etwas leiser.
- **Brightness:** Die Reflexionen werden bei +50/+100 % weniger hell als der Nachhall (Schwerpunkt ×1,5 statt ×2,3).
- **Vintage:** Der späte Hall ist oben herum heller als bei Modern.

## Was falsch war

| Fehler | Wirkung | Lösung |
|---|---|---|
| Der Aufbau des Nachhalls war eine Summe von 6 kurz versetzten Kopien des Eingangs | Kammfilter: in großen Räumen +7 dB Bass und eine Senke von rund 4 dB bei 250–500 Hz. Die Klangfilter je Raum mussten das mit bis zu +11 dB ausgleichen | Jede der 16 Hall‑Leitungen bekommt den Eingang zu ihrer eigenen Zeit. Kein Kammfilter mehr, die Ausgleichsfilter sind jetzt klein (große Räume: Bass +0,5 … +2,5 dB) |
| Reflexionen lagen zwischen zwei Samples | Das Zwischenrechnen schluckt Höhen (−3 dB bei 16 kHz). Reflexionen bei Distance 0 % zu dunkel (7,1 statt 9,5 kHz) | Reflexionen auf ganzen Samples, Filter bis 22 kHz. Jetzt 9,5 kHz wie Pro‑R |
| Nur 10 einzelne Reflexionen bis 65 ms, mit Lücke bei 20–30 ms | In großen Räumen 3–6 Reflexionen zwischen 20 und 50 ms statt etwa 29 | Zug aus 112 einzelnen Reflexionen bis etwa 100 ms (zufällige Zeiten mit festem Startwert, keine Kopie der Pro‑R‑Zeiten). Pegel folgen der Hallzeit |

## Was neu abgeglichen wird

| Abgleich | Was | Woran gemessen |
|---|---|---|
| `fit density` | Raumgröße = Länge der Hall‑Leitungen, je Raum | Echodichte 20–50, 50–100, 100–200 ms |
| `fit earlytap` | frühe Abgriffe, Reflexions‑Pegel, Aufbau, Reflexions‑Filter | C50, Ts, frühe Abklingzeit, Echodichte, Helligkeit 0–30 ms |
| `fit ercut` | Reflexions‑Filter je Raum (mehrere Schritte) | Helligkeit 0–30 ms |
| `fit primaryc50` | Pegel der ersten starken Reflexionen je Raum (nur mittlere und große Räume übernommen) | Klarheit C50 |
| `fit tone` | Klangfilter des Nachhalls je Raum | Klang je Oktave **und** Helligkeit des späten Halls (0,1–0,4 s) |
| `fit charer` | Pegel und Helligkeit der Reflexionen außerhalb Character 50 % | C50, Helligkeit 0–30 ms |
| `fit styletone vintage/plate` | Klangfilter je Style‑Punkt (0,75 / 1,85 / 10 s) | Klang je Oktave, Helligkeit des späten Halls |
| `fit thickdens` | Thickness: weniger Reflexionen unter 0 %, zusätzliche über 0 %, Streuung, frühe Abgriffe | Echodichte, C50, frühe Abklingzeit |
| `fit distance` | zusätzlich Streuung je Distance‑Punkt, längerer Aufbau weit weg | wie bisher + Echodichte |
| `fit brightness` | zusätzlich eigenes Höhen‑Filter für die Reflexionen | wie bisher + Helligkeit des späten Halls |
| Pegel‑Feinabgleich | Pegel je Raum und je Stellung von Decay Rate, Width, Thickness (±50 %), Distance, Brightness, Character; Höhen des späten Halls je Decay‑Rate‑Stufe | Pegel, Helligkeit des späten Halls |

Vintage behält seine bisherigen Raummaße (es war damit schon genau abgeglichen). Die Modulation (Character) wurde für die längeren Leitungen neu eingestellt.

## Ergebnis

Mittlere Abweichung zu Pro‑R über alle passenden Aufnahmen (`NachhallReference match`).

| Messgröße | Stand 20.11. | jetzt | schlechteste Aufnahme jetzt |
|---|---|---|---|
| Hallzeit 500 Hz – 2 kHz | 1,45 % | **1,2 %** | 4,9 % (Brightness −100 %) |
| Hallzeit je Oktave 125 Hz – 8 kHz | 2,4 % | **2,3 %** | 14 % (Decay Rate 25 %) |
| Pegel | 0,15 dB | **0,10 dB** | 2,3 dB (Thickness +100 %, Impuls) |
| Klarheit C50 | 0,58 dB | **0,39 dB** | 2,0 dB (Raum 0,75 s) |
| Frühe Abklingzeit, Mitten | 6,2 % | **3,9 %** | 14 % (Distance 0 %; vorher 39 %) |
| Schwerpunktzeit Ts | 7,0 % | **5,1 %** | 18 % (Thickness −100 %) |
| Frühe Abklingzeit, alle Frequenzen | 8,4 % | **6,0 %** | 24 % (Decay Rate 25 %) |
| Klangbalance 63 Hz – 8 kHz | 1,24 dB | **0,75 dB** | 3,2 dB (Brightness −100 %; vorher 10,6 dB) |
| Übersprechen (Width) | 0,20 dB | **0,08 dB** | 0,13 dB |
| Tonbreite 1 kHz (Character) | 1,83 Hz | **0,76 Hz** | 1,6 Hz |
| Thickness laut/leise | 0,10 dB | 0,64 dB | 1,2 dB |
| Styles: Hallzeit / Pegel / C50 | 0,44 % / 0,09 dB / 0,91 dB | 0,54 % / **0,00 dB** / **0,25 dB** | 1,2 % / 0,0 dB / 0,5 dB (vorher 3,6 dB, kleiner Plate) |
| Echodichte | 0,16 (20–100 ms) | **0,06** (20–200 ms) | 0,23 (Brightness −100 %) |
| Helligkeit 0–30 ms | 8,6 % | **2,0 %** | 17 % (Brightness −100 %; vorher 42 %) |

Weitere Zahlen (Nachhall / Pro‑R):

- Halle 1,85 s: Echodichte 20–50 / 50–100 / 100–200 ms 0,23 / 0,34 / 0,62 (Pro‑R 0,21 / 0,35 / 0,54). Helligkeit des späten Halls 3.079 / 3.151 Hz.
- Größter Raum (10 s): 20–50 ms 29 einzelne Reflexionen (Pro‑R 29, vorher 3–4).
- Thickness −100 / +100 %: Echodichte 20–50 ms 0,07 / 0,42 (Pro‑R 0,06 / 0,47).
- Distance 0 %: Helligkeit der Reflexionen 9.511 Hz (Pro‑R 9.505 Hz, vorher 7.102 Hz).
- Character 0 %: Helligkeit der Reflexionen 4.679 Hz (Pro‑R 4.574 Hz, vorher 6.518 Hz).
- Decay Rate 25 %: Helligkeit des späten Halls 4.331 Hz (Pro‑R 4.263 Hz, vorher 2.990 Hz).
- Über 12 kHz im späten Hall: Modern −16,3 dB, Vintage −15,9 dB (Pro‑R −16,8 / −12,4 dB). Modern passt jetzt auf 0,5 dB (vorher rund 5 dB daneben).
- Rechenlast (ein Kern, 48 kHz): Modern 3,5 %, Vintage 4,4 %, Plate 3,2 %.

Alle Prüfungen bestehen: `match` (jetzt 16 Messgrößen), 32 Verhaltensprüfungen, alle DSP‑Tests.

## Was noch anders ist

- **Distance 0 %:** Klarheit +0,8 dB, frühe Abklingzeit +13 %. **Distance 100 %:** Pro‑R baut sehr langsam auf (Schwerpunktzeit 230 ms, Nachhall 199 ms), Klarheit −15,2 statt −16,0 dB.
- **Decay Rate 25 %:** Nachhall fällt bei hohen Frequenzen am Anfang schneller ab als Pro‑R (frühe Abklingzeit aller Frequenzen: Nachhall 0,53 s, Pro‑R 0,71 s).
- **Kleine Räume (0,4 und 0,75 s):** Klarheit etwa 2 dB zu niedrig. Pro‑R hat dort eine einzelne, sehr starke Reflexion bei rund 28 ms. Das zufällige Muster von Nachhall bildet so etwas nicht nach (bewusst: keine Kopie der Pro‑R‑Zeiten). Lautere erste Reflexionen treffen zwar die Klarheit, machen aber frühe Abklingzeit und Schwerpunktzeit 15–20 % zu kurz; deshalb dort nicht genutzt.
- **Thickness ±100 %:** Pro‑R regelt einen einzelnen Impuls etwas anders als Dauersignale. Nachhall trifft die Dauersignale (laut/leise auf ±1,2 dB); der Impuls ist bei +100 % 2,3 dB leiser als bei Pro‑R.
- **Brightness −100 %:** Anfang weniger dicht als bei Pro‑R (Echodichte 20–50 ms 0,41 statt 0,65), Klarheit +0,8 dB. **+50/+100 %:** der späte Hall wird ×1,9 heller, bei Pro‑R ×2,4 (vorher ×1,7). Die Reflexionen passen (×1,5 wie Pro‑R, vorher ×1,9).
- **Großer Vintage‑Raum:** Anfang dünner als bei Pro‑R (Echodichte 20–50 ms 0,15 statt 0,39).
- **Modulation ist zufällig.** Einzelne Messungen schwanken um ±1–2 Hz. Verglichen wird der Mittelwert über 8 Durchläufe.
