# Data and Evaluation Plan

*Vehicle Plate Recognition, version 0.1, 4 November 2026*

## 1. Why this matters

Collecting and labelling data will probably take more time than writing code, and the 95% target depends on it. This plan defines how data is collected and how success is measured, so the final number can be trusted and repeated.

## 2. Data sources

| Source | Content | Use | To check first |
| --- | --- | --- | --- |
| Filming in Yerevan | Armenian plates in real traffic | Training and testing | Filming rules, section 6 |
| Filming in Iran, by an acquaintance | Iranian plates in real traffic | Training and testing | Written guide, local rules |
| IR-LPR, github.com/mut-deep/IR-LPR | About 20,000 car images with plate and character labels. The two published versions give slightly different counts | Training for Iran | Licence and commercial use |
| Iranis, github.com/alitourani/Iranis-dataset | More than 83,000 Persian plate character images | Training the Iranian reader | Licence and commercial use |

No public Armenian plate dataset was found. A 2023 student project at the American University of Armenia also reported this shortage and collected its own data. Armenian plates must therefore come from our own filming.

## 3. Plate formats

| Country | Format of private plates | Example | Note |
| --- | --- | --- | --- |
| Armenia | 2 digits, 2 Latin letters, 3 digits | 12 AB 345 | Confirmed on Yerevan footage, where it is the most common series |
| Armenia | 3 digits, 2 Latin letters, 2 digits | 123 AB 45 | Common on Yerevan footage. Secondary sources call it a government series, but the project owner confirmed on 5 November 2026 that it is private |
| Iran | 2 digits, 1 Persian letter, 3 digits, 2 digit region code | 12 ‎ب‎ 345 67 | The list of private letters in the code must be confirmed with labelled data |

## 4. Filming guide

These settings apply in both countries, so the two results can be compared.
- Fix the phone on a tripod or a firm mount. It must not be held in the hand.
- Film from a footbridge, a window or the roadside, facing the traffic at a small angle, so each plate faces the camera.
- Use at least 1080p and 30 frames per second, and 60 if available.
- Set the shortest exposure the light allows, to reduce motion blur.
- Film in daylight, on city streets up to about 60 km/h, with several lanes when possible.
- Record 15 to 30 minutes per session, at several places, on several days, and at different times of day.
- Keep the original files. Record date, place, phone model and settings in the data register.
- Do not film people on purpose, and do not stay where filming is not allowed.

## 5. Splitting and labelling

- Split by session, not by frame. The test set comes only from a place or day never used for training. Otherwise the result looks better than it really is.
- Proposed test set: at least 300 vehicles per country. The test set is used only for official measurements, not for tuning.
- For testing, label each vehicle crossing the line with: plate text, whether a person can read it, whether the plate is visible, and its plate type.
- For training, label boxes for vehicles, plates and characters. CVAT and Label Studio are candidate free tools.
- A second person, or a second pass on another day, checks a sample of the test labels.

## 6. Privacy and law

Plate numbers and road footage can identify people. Armenia has a Law on the Protection of Personal Data (No. 49-ZR, 2015) and a Personal Data Protection Agency. Iran and any customer country need their own check. This section is a working rule, not legal advice, and we have not found a source that settles how plates are treated under Armenian law. Legal advice is needed before a pilot or a sale.
- Footage and labels are never placed in the public repository. Git is set to ignore them.
- The demonstration video uses blurred plates and faces, or footage with consent.
- Stored events and photos are deleted after a set period. The test application uses 30 days.
- Access to raw footage is limited to the people who need it.

## 7. Measures

Each measure is reported separately for each country. A combined number must not hide a weak country.

| Measure | Formula | Target |
| --- | --- | --- |
| Read accuracy | Plates fully correct / plates a person can read and that crossed the line | At least 95% |
| Coverage | Vehicles with a plate event / vehicles crossing the line with a visible plate | At least 90% |
| Vehicle count error | Events without a real vehicle, plus duplicate events, plus missed vehicles | Reported, as low as possible |
| Character error | Share of wrong characters, used to find the weakest letters and digits | Reported |
| Processing speed | Frames per second and dropped frames, on CPU and on GPU, with hardware named | Proposed: no vehicle lost on the CPU |

A plate counts as correct only when all of its characters are correct. Vehicles with no visible plate are listed separately and are not hidden in either measure. Plates of other types are counted separately as out of scope.

## 8. Evaluation procedure

- The evaluation tool, still to be built, runs each test video through the library and compares events with the labels.
- Video must be processed in a repeatable way, so results can be compared between versions. Playback through Qt Multimedia follows real time, so the tool will need a frame by frame decoder.
- Each official run records the version of code and models, hardware, date and results.
- A report is written for each run. Errors are sorted by cause: vehicle missed, track lost, plate not found, character wrong.
- The next round of work goes to the cause that loses the most plates.

## 9. Reference machine

Measurements on the development laptop use an Intel Core i5-13420H, 32 GB of memory, and an NVIDIA GeForce RTX 3050 Laptop GPU with 6 GB. CPU results must also be measured with the GPU unused.

## 10. Sources

- arxiv.org/abs/2209.04680, IR-LPR dataset
- arxiv.org/abs/2101.00295, Iranis dataset
- en.wikipedia.org/wiki/Vehicle_registration_plates_of_Armenia
- cse.aua.am/files/2023/12/Vehicle_License_Plate_Recognition_in_Armenia-Diana-Sargsyan.pdf
- dataguidance.com/notes/armenia-data-protection-overview
- onnxruntime.ai/docs/execution-providers
