# ShutterTrail for Premiere Pro

<img src="./shuttertrail-avatar.png" alt="ShutterTrail" width="128" height="128">

En lokal Windows-panel för att ge ett trimmat klipp rörelseoskärpa i Adobe Premiere Pro. Markera klippet, välj slutarvinkel och rendera. Panelen importerar en ljudlös MP4 till samma tidsintervall på spåret direkt ovanför. Du kan växla mellan bilderna genom att slå av och på det övre spåret.

## Kom igång på Windows 11 x64

Det enkla sättet kräver inte Visual Studio Build Tools.

1. Ladda ner artefakten `shuttertrail-premiere-windows` från en grön körning av [Windows-arbetsflödet](https://github.com/jojo-swe/shuttertrail-premiere/actions/workflows/windows.yml). Samma paket kan också ligga under [Releases](https://github.com/jojo-swe/shuttertrail-premiere/releases) när ett släpp har publicerats där. Paketet innehåller `panel\`, `build\MotionBlurWorker.exe`, `Install.cmd`, `LICENSE` och `README.md`. Det innehåller inte testprogram, testfilm eller loggar.
2. Packa upp paketet till en skrivbar mapp och stäng Premiere. Dubbelklicka på **Install.cmd**. Finns `build\MotionBlurWorker.exe` används den och **Build.cmd** körs inte. Saknas den men `bin\MotionBlurWorker.exe` finns, används den i stället. Saknas båda bygger **Install.cmd** workern med **Build.cmd** innan installationen.
3. Starta en Premiere-version med CEP 11 eller 12. Öppna **Window → Extensions → Motion Blur**. Målgruppen är Premiere Pro 23–26 på värdar där CEP finns kvar. Versionsintervallet i manifestet är ingen verifierad kompatibilitetsmatris. Adobe har gått över till UXP för ny utveckling.
4. Spara Premiere-projektet. Lägg ett kort, progressivt SDR-klipp i H.264 med konstant bildfrekvens på V1 i en sekvens med samma bildfrekvens. Skapa V2 om det behövs. Trimma gärna bort den första sekunden. Lämna intervallet på V2 ledigt och olåst.
5. Markera klippet och börja med **180° och 8 prover**. Klicka på **Rendera till spåret ovanför**. Första resultatet sparas under `Motion Blur Renders` bredvid projektfilen. Mappen är en del av projektets media när du flyttar eller arkiverar projektet.

Bygg från källkod bara när ingen färdig worker följer med. Installera då **Visual Studio 2022 Build Tools** med arbetsbelastningen **Desktop development with C++**, x64-kompilatorn och ett aktuellt Windows 10- eller 11-SDK. I Windows N behövs även **Media Feature Pack**. Packa upp källträdet och ta bort en eventuell `build\MotionBlurWorker.exe` eller `bin\MotionBlurWorker.exe`, annars installeras den filen utan ombyggnad. Kör sedan **Install.cmd**. Skriptet anropar **Build.cmd**, som länkar workern med statisk C/C++-runtime (`/MT`) och kör både bildtestet och ett riktigt H.264-test före installation. Den färdiga workern behöver inte Microsoft Visual C++ Redistributable.

Installationen kopierar panelen till `%APPDATA%\Adobe\CEP\extensions\se.jojoswe.motionblur` och aktiverar utvecklarläge för osignerade paneler genom användarens `PlayerDebugMode` i `HKCU\Software\Adobe\CSXS.11` och `CSXS.12`. Inställningen gäller även andra CEP-paneler för samma användare. För avinstallation, stäng Premiere och ta bort den installerade panelmappen. Återställ registervärdena till tidigare läge om du inte behöver dem för andra paneler.

Byggverktyg: https://visualstudio.microsoft.com/visual-cpp-build-tools/  
Premieres panelstöd: https://github.com/Adobe-CEP/Samples/tree/master/PProPanel

## Bildresultat och begränsningar

- **Slutarvinkel.** 180° simulerar exponering under en halv bildperiod, lika långt före och efter rutans mitt. Vid 25 fps motsvarar det 1/50 s. 0° ger en referens med samma H.264-omkodning. Redan inbakad oskärpa i källan finns kvar.
- **4, 8 eller 16 prover.** Fler tidpunkter gör sammanvägningen finare och tar längre tid. Renderfilen har sekvensens bildfrekvens, även vid 24000/1001, 30000/1001 och 60000/1001. Motorn arbetar bildruta för bildruta i bakgrunden.
- **Metod.** Motorn söker lokala förflyttningar i båda tidsriktningarna med blockmatchning på luminans, uppskattar delbilder vid provtidpunkterna och beräknar medelvärdet. Svaga matchningar vägs ned. Första och sista rutan får bara använda bilder inom trimningen. Detektorn för hårda klipp jämför luminanshistogram och kan missa klipp med liknande bildinnehåll. Snabba förflyttningar, upprepade mönster och objekt som skymmer varandra kan ge dubbla konturer. Beräkningen sker på CPU i videons kodade färgrymd.
- **Källmaterial.** Den här versionen är avsedd för vanliga lokala H.264-klipp i MP4, 8-bitars SDR, kvadratiska pixlar, en videoström, spelhastighet 100 % och samma bildfrekvens som sekvensen. Renderingen skriver H.264 High, 4:2:0, utan ljud. Windows lokala avkodare och kodare bestämmer vilka storlekar de klarar.
- **Premiere-effekter.** Panelen läser originalfilens bild före Lumetri, Motion/Scale, masker, opacitet, stabilisering och andra klippeffekter. Använd obehandlade klipp eller baka först in de effekter som ska synas. Speed ramps, underklipp med annan tidsbas, omtolkat material, nestade sekvenser, bildsekvenser, VFR, HDR, roterade mobilklipp och multicam ingår inte i stödet.
- **Placering.** Behåll klippet markerat under jobbet. Panelen kontrollerar sekvens, klipp, trim och ledigt intervall igen före placeringen. Det finns ingen reservplacering på källspåret. Vid placeringsfel visas sökvägen till den färdiga filmen så att du kan använda den manuellt.

## Licens och innehåll

All projektspecifik källkod, skript, tester och dokumentation i detta repo släpps under MIT. Licenstexten finns i [LICENSE](LICENSE). Repot innehåller inga importerade kodbibliotek, tränade vikter, incheckade binärer, typsnitt, testklipp eller SDK-filer. Testfilmen skapas lokalt av testet. Windows-arbetsflödet kan publicera ett installationspaket med den kompilerade workern.

| Del | Ursprung och distribution |
| --- | --- |
| Panel, rörelseberäkning, renderare, tester | Egen kod i repot, MIT |
| Premiere Pro och CEP | Installerad Adobe-värd med egna villkor |
| Media Foundation och H.264-codec | Windows systemkomponenter med egna villkor |
| MSVC, standardbibliotek, Windows SDK, WRL | Byggverktyg och plattformsgränssnitt, bara när workern kompileras från källkod. `MotionBlurWorker.exe` länkas med `/MT`, så C/C++-runtime ingår i exe-filen. Målmaskinen behöver inte Microsoft Visual C++ Redistributable. Microsofts runtime är inte MIT och ingår inte som källkod i repot; den följer med den byggda exe-filen enligt villkoren för Visual Studio Build Tools. Projektets egen kod är MIT |
| FFmpeg, RIFE, ncnn, Blur och Flowframes | Används inte |

MIT gäller materialet i repot. Adobe och Microsoft tillhandahåller värdprogram, verktyg och systemkomponenter separat. Installationspaketet från arbetsflödet innehåller panelen, `Install.cmd`, `LICENSE`, `README.md` och `build\MotionBlurWorker.exe` byggd med `/MT`. Paketet innehåller inte FFmpeg, RIFE, ncnn, modellvikter eller andra tredjepartsbibliotek.

## Utveckling och verifiering

- **Build.cmd** bygger endast i `build/` och kör Windows-testerna. Det installerar ingen panel. Workern och testprogrammen länkas med `/MT`. Windows-testet skapar en 60-rutors H.264-video i 30000/1001 fps, renderar nio rutor från källruta 37, avkodar resultatet och kontrollerar bildinnehåll, tidsstämplar och antal bildrutor.
- **Install.cmd** kopierar panelen till CEP-mappen och `MotionBlurWorker.exe` till panelens `bin\`. Finns en färdig worker i `build\` används den. Annars används `bin\MotionBlurWorker.exe` om den finns. Saknas båda körs **Build.cmd** först.
- **tests/MotionBlurTest.cpp** provar statisk bild, känd förflyttning, två exponeringslängder, 0°, neutral krominans, klippgräns och en enskild bildruta. På Linux/macOS kan det köras med `c++ -std=c++17 -O2 tests/MotionBlurTest.cpp -o /tmp/motion-test && /tmp/motion-test`.
- **tests/TimelineTest.js** kör ExtendScript-koden mot en simulerad värd. Kör `node tests/TimelineTest.js`. Testet kontrollerar även trim, saknat eller låst målspår, överlapp och ändrad sekvens.
- GitHub Actions kör Windows-bygget och testerna vid push till ett publikt repo. När båda har gått igenom laddas installationspaketet upp som artefakten `shuttertrail-premiere-windows`. Värdintegrationen behöver dessutom provas i den Premiere-version du tänker stödja. Jämför särskilt in/ut vid trimning, bildens färg och placeringen vid 29,97 fps.

Panelens UI finns i `panel/`, tidslinjeanropen i `panel/jsx/Timeline.jsx`, rörelseberäkningen i `worker/MotionBlur.hpp` och Windows-kopplingen i `worker/RenderClip.cpp`.
