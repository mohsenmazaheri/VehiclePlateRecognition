# Software Architecture

*Vehicle Plate Recognition, version 0.1, 4 November 2026*

## 1. Overview

The system is one pipeline: frames come in, vehicles are detected and tracked, plates are read and combined per vehicle, and one event is produced when a vehicle crosses the line. Everything outside this logic sits behind an interface, called a port, so cameras, models and storage can change without touching the core.

## 2. Layers

Dependencies point inwards only. An automated test fails the build when an include breaks this rule, and this check was confirmed by adding a forbidden include on purpose.

| Layer | Folder | Contains | May depend on |
| --- | --- | --- | --- |
| Domain | src/domain | Images, tracks, events, ports, plate formats | Standard library only |
| Application | src/application | PassagePipeline, LineCrossingDetector, PlateVoter | Domain |
| Tracking adapter | src/infrastructure/tracking | ByteTracker | Domain |
| Qt adapters | src/infrastructure/qt | Frame sources, output files, SQLite, worker thread | Application, Qt |
| ONNX adapter | src/infrastructure/onnx | YoloxVehicleDetector, YoloV9PlateDetector, SlotPlateReader, model input and output | Domain, ONNX Runtime |
| Simulation | src/simulation | Synthetic scene for tests and demonstration | Domain |
| Application shell | apps/test-ui | Test application and composition root | All layers |

## 3. Ports

| Port | Responsibility | Implementations today |
| --- | --- | --- |
| IFrameSource | Deliver frames from any camera or video | MediaUrlFrameSource, CameraFrameSource, SyntheticFrameSource |
| IVehicleDetector | Find vehicles in a frame | YoloxVehicleDetector, and simulation for tests |
| ITracker | Keep vehicle identity across frames | ByteTracker |
| IPlateDetector | Find the plate in a vehicle crop | YoloV9PlateDetector with a trial model, and simulation for tests |
| IPlateReader | Read the text of a plate crop | SlotPlateReader with a trial model for Latin letters, and simulation for tests |
| IPlateFormat | Validate and display the text of one plate format | PatternPlateFormat |
| IPassageSink | Receive finished events | EvidenceSink, SqlitePassageRepository |
| IPassageRepository | Search and delete stored events | SqlitePassageRepository |

## 4. Processing of one frame

- The worker thread takes the newest frame. Older waiting frames are dropped and counted.
- The vehicle detector returns boxes, and the tracker turns them into stable tracks.
- Plates are searched in every second processed frame by default. A limit on reads per frame can be set: the test application allows one, because one plate costs about 45 ms on the CPU. The vehicles nearest the line take turns, three per allowed read.
- A plate found in a vehicle crop is used only if it belongs to that vehicle: of all vehicle boxes that contain it, it must be relatively nearest to the bottom of this one. Vehicle boxes overlap in traffic, and without this check a vehicle took the plate of the one behind it.
- A reading below the minimum reading confidence is dropped. The test application uses 0.9. The rest is added to the voter of that vehicle.
- The photo is the vehicle crop with the largest clear view of its plate. Until a plate is found, it is the largest view of the vehicle.
- A vehicle crosses when the bottom centre of its box changes side of the line, within the ends of the line.
- Reading continues for 5 more frames by default, then one event is produced. If the track ends sooner, the event is produced then. The test application turns on immediate reporting: when the plate is already read in a configured format at the crossing, the event is produced in the crossing frame, and only vehicles without a valid reading wait up to 5 frames. The plate, the plate photo and the vehicle photo show in the application at once, and the table lists the passages of this run only. Search finds earlier stored results.

## 5. Design decisions

| Decision | Reason |
| --- | --- |
| Side change instead of touching the line | It still works when frames are dropped |
| One event per track, kept when the line moves | Moving the line can never count a vehicle twice |
| Voting across frames | One blurred frame must not decide the plate |
| Readings that fit a format are trusted first, as long as the other readings look like misreads of them | The plate format is strong knowledge that corrects many errors. On road video a bus whose plate fits no format once read the plate of the car beside it, and that single reading must not win |
| Drop unsure readings | A blurred plate is often read as another plate that still fits the format. No plate read is better than a wrong number |
| A budget for plate reading | Reading every vehicle in every frame dropped processing to about 2 frames per second, and the tracker then loses vehicles |
| Model threads do not spin while idle | Several models run one after another. With spinning, one model's idle threads took the processor from the next, and processing fell from about 11 to about 3 frames per second |
| Three separate results: read, unknown format, not visible | A system that only reports easy plates must not look accurate. This supports the coverage measure |
| Tracker prediction based on time | Tracks survive dropped frames |
| Formats defined as data | A new country needs a definition, not new core code |
| Line in normalized coordinates | It survives a change of resolution |

## 6. How SOLID is applied

| Principle | In this project |
| --- | --- |
| Single responsibility | Line crossing, voting, tracking and storage are separate classes |
| Open closed | New camera, model or country means a new class or definition, with no change to the pipeline |
| Liskov substitution | Simulated and real detectors are used through the same port. The tests rely on this |
| Interface segregation | Small ports with one job each. Storage search is separate from event receiving |
| Dependency inversion | PassagePipeline depends only on ports. Concrete classes are chosen in the composition root |

## 7. Models

All three models run on real Yerevan footage. Results are in section 11.
- Runtime: ONNX Runtime 1.31.1, MIT licence. Only the CPU is used so far. The plan is to try the listed execution providers in order, fall back to the CPU, and show the chosen provider to the user, because the fallback can be silent.
- Vehicle detector: the published YOLOX-S model, Apache-2.0, keeping car, motorcycle, bus and truck.
- Plate detector, trial: yolo-v9-t-640-license-plates-end2end from the open-image-models project. About 33 ms per vehicle on the CPU.
- Plate reader, trial: cct_s_v2_global from the fast-plate-ocr project. One classifier per character position, Latin letters and digits only, and Armenia is among its training countries. About 12 ms per plate. It cannot read Iranian plates.
- Licence of the trial models: both projects use the MIT licence for their code, but the plate detector comes from YOLOv9, whose original repository is GPL-3.0, and a third party model card states that the reader's training data is non commercial (CC-BY-NC-ND-4.0). This has not been confirmed at the source. They are used for trials only, not for a customer pilot.
- Planned: our own plate detector and reader, trained on our data under licences that allow commercial use. A reader for Iran needs its own alphabet. The reader adapter only needs another config file for that.
- Model files and trained weights stay out of git.

## 8. Threads

- Frame sources run on the main thread, where Qt Multimedia delivers frames.
- PipelineWorker runs on its own thread and owns the models and the database connection it creates.
- The application window uses its own database connection for search.

## 9. How to extend

### Add a camera

Implement IFrameSource, create it in the composition root, and call the frame handler with each frame. No other change is needed.

### Add a country

Add a PatternPlateFormat definition, such as country code, pattern and display template. Then train the reader on its characters. An automated test shows that the format part needs no core change.

### Add a model

Implement the matching port in a new adapter. Only the composition root changes.

## 10. Tests and CI

| Check | Covers | Status |
| --- | --- | --- |
| test_core, 29 tests | Formats, voting, tracking, line crossing, reading budget, plate ownership, photo choice, unsure readings, immediate reporting at the crossing, full pipeline with synthetic data | Passing on Windows, MinGW 13.1, Qt 6.12 |
| test_plate_core, 11 tests | Plate model input preparation, box decoding, reader config, text decoding, Persian alphabet, wrong shapes | Passing on Windows, MinGW 13.1, Qt 6.12 |
| test_real_plates, 4 tests | Real plate models load and run. With hand checked photos in data/plate-samples, every plate must be read right | Passing on the same setup with 14 of 14 photos read right. The photo part skips on CI because the photos are private |
| test_qt_adapters, 7 tests | Image conversion, output files, database, deletion, full chain on a thread | Passing on Windows, MinGW 13.1, Qt 6.12 |
| test_yolox_core, 7 tests | YOLOX input preparation, output decoding, overlap removal, wrong shapes | Passing on Windows, MinGW 13.1, Qt 6.12 |
| test_real_detector, 3 tests | Real YOLOX-S on one sample picture, real detections crossing the line | Passing on the same setup, skipped when the model is not downloaded |
| test_video_file, 1 test | A generated MP4 through decoder, worker thread, YOLOX, tracker and line | Passing on the same setup, skipped when the model is not downloaded or no encoder exists |
| architecture_rules | Direction of includes | Passing, and confirmed to fail on a violation |
| GitHub Actions | Windows with MSVC and Qt 6.8, Linux with GCC and Qt 6.8 | Written, not yet run |

## 11. First run on road footage

Seven phone videos from Yerevan, about 9.5 minutes in total, 1920 x 1080 at 30 frames per second, run on 5 November 2026 on a laptop CPU with the default line low in the picture. There is no labelled ground truth yet, so these are counts and an eye check, not a measured accuracy.

| Result | Count |
| --- | --- |
| Vehicles crossing the line | 229 |
| Read, private Armenian format | 191 |
| Unknown format | 8 |
| No plate read | 30 |

- Unknown format holds yellow plates of buses and other public vehicles and foreign plates such as Georgian ones. The text of these was mostly read right, but it fits no configured format.
- The 3 digit, 2 letter, 2 digit series was added as a private format after this run. The counts above were recounted from the stored results of the run, which moved 24 passages from unknown format to read. A new run may differ slightly, because voting also uses the formats.
- 12 of the 30 without a plate come from the two most blurred videos. There the 0.9 reading threshold turned readings that were mostly wrong into no plate read. It also dropped a few plates that a person can still read, so the threshold must be tuned on a labelled test set.
- An eye check of the plate photo against the text, over all passages of an earlier run with the same models, found one wrong read: a bus whose plate fits no format took the plate of the car beside it. The voter now rejects this case. A check by eye at this size can miss single characters, so it does not replace the evaluation in the data plan.
- The same vehicle was sometimes reported twice, when its track was lost and found again. At least two cases were seen. This is not handled yet.
- Processing speed was about 4 to 11 frames per second, with the rest dropped by design. Some runs shared the processor with builds, so the lower numbers are not reliable.
- Vehicles that crossed without being detected or tracked have not been counted, so coverage is unknown.
