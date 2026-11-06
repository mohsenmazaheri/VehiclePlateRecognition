# Project Charter and Requirements

*Vehicle Plate Recognition, version 0.1, 4 November 2026*

## 1. Purpose

Build a real product: a cross platform C++ and Qt library that tracks vehicles in video and, when a vehicle crosses an adjustable line, reports its plate number with a photo of the vehicle. The first target is road traffic in Armenia and Iran. A small test application exercises the library.

## 2. Current status

The architecture, tracker, line crossing, plate voting, storage, camera sources and test application work and pass automated tests on Windows with a synthetic scene. The synthetic detectors return scripted truth.
- Vehicle detection runs with a pretrained YOLOX-S model on the CPU. GPU use is not connected yet.
- Plates are found and read with two published trial models. On seven Yerevan videos, 229 vehicles crossed the line and 191 plates were read in a private Armenian format. Accuracy has not been measured, because no labelled test set exists yet. See section 11 of the architecture document.
- The trial reader knows Latin letters only, so Iranian plates cannot be read yet. The licences of the trial models are not settled for commercial use.
- The code is public at github.com/mohsenmazaheri/VehiclePlateRecognition since 6 November 2026, without a licence for now.

## 3. Scope of version 1

### In scope

- Standard private plates of Armenia and Iran.
- Daylight with good light, city streets up to about 60 km/h, several lanes at once, a fixed phone camera.
- Video file, webcam and RTSP stream as image sources.
- Automatic use of a GPU when present, otherwise the CPU.
- Windows and Linux.

### Out of scope

- Night, rain, snow and heavily dirty plates.
- Highway speeds above about 60 km/h.
- Taxi, public transport, government and other plate types. These are reported as unknown and counted separately.
- Vendor camera SDKs. The design allows them, but none is included.
- A customer installation. The first pilot is a separate goal that follows version 1.

## 4. Success criteria

Version 1 is complete when every line below is shown by measurement or by a repeatable check. The target date is 2 to 3 months from the start, that is by early February 2027.

| Criterion | Required result | How it is shown |
| --- | --- | --- |
| Read accuracy | At least 95% of plates fully correct, among plates a person can read, for each country separately | Evaluation report on the test set |
| Coverage | A plate event for at least 90% of vehicles crossing the line. Vehicles with no visible plate are counted separately | Evaluation report on the test set |
| One event per vehicle | Exactly one event with plate number and vehicle photo for each crossing | Automated tests and evaluation report |
| Test data | Video from Yerevan and from Iran, filmed in a similar way. Proposed: at least 300 vehicles per country, from a place or day not used for training | Data register |
| Speed | Proposed: no vehicle lost because of slow processing on the CPU, even when frames are skipped | Evaluation on CPU and on GPU |
| Cameras | A new image source is added without changing the core | Three sources behind one interface |
| Engineering | CI green on Windows and Linux, automated tests, dependency rule enforced, licences reviewed | CI history and licence list |
| Demonstration | Video in the README showing tracking, line crossing and output, with plates blurred or with consent | README |

Values marked as proposed were suggested during planning and are the working assumption until changed.

## 5. What makes the project more valuable

- A first pilot on a real customer camera.
- Public evidence of computer vision, C++ and architecture skills for job interviews.
- Adding another country through a new format and model, with no change to the core.

## 6. Functional requirements

| ID | Requirement | Status |
| --- | --- | --- |
| F1 | Accept frames from any source through one interface | Done |
| F2 | Read a video file | Done, tested with 7 phone videos from Yerevan, 1920 x 1080 at 30 frames per second |
| F3 | Read a webcam | Written, not yet tested with a camera |
| F4 | Read an RTSP stream | Written, not yet tested with a stream |
| F5 | Detect vehicles with a YOLO family model | Done with YOLOX-S, run on 7 Yerevan videos. Vehicles missed have not been counted |
| F6 | Track each vehicle with a stable identity | Done, tested with synthetic data |
| F7 | Adjustable counting line, also while running | Done |
| F8 | Detect the plate within the vehicle | Works with a trial model on Armenian footage. Own model planned |
| F9 | Read the plate and combine readings across frames | Works with a trial reader for Latin letters on Armenian footage. Iranian reader planned |
| F10 | Check the text against the format of each country | Done, Iranian letter list to be confirmed |
| F11 | Output through a C++ callback, JSON file and photos | Done |
| F12 | Store events in a local database, searchable in the application | Done |
| F13 | Delete stored events and photos after a set period | Done, 30 days in the test application |

## 7. Non functional requirements

- C++20 and Qt 6, on Windows and Linux.
- Clean Architecture and SOLID, with the dependency rule enforced by an automated check.
- The processing thread keeps only the newest frame, so a slow machine loses frames rather than building up delay.
- Models and libraries must allow commercial use. Ultralytics models are excluded because of AGPL-3.0, unless a commercial licence is bought.

## 8. Roadmap

| Phase | Weeks | Result |
| --- | --- | --- |
| 1. Foundation | 1 | Core, test application and documents. Done, published on GitHub |
| 2. Vehicle detection | 2 to 3 | YOLOX through ONNX Runtime, tested with real video from Yerevan. Done, with trial plate models added early |
| 3. Data | 2 to 7 | Filming and labelling in both countries, running alongside other phases |
| 4. Plate models | 4 to 8 | Plate detector and character reader trained for both countries |
| 5. Evaluation | 8 to 10 | Evaluation tool, first baseline, then improvement rounds |
| 6. Release | 10 to 12 | Demonstration, version 1 report, Linux checks |

## 9. Main risks

| Risk | Effect | Response |
| --- | --- | --- |
| 95% is not reached in the available time | Version 1 is late or the target changes | Measure early, report the real number, improve the weakest step first |
| Too little Armenian data | Low accuracy on Armenian plates | Film early and often. No public Armenian dataset was found |
| Motion blur with a phone camera | Plates cannot be read | City speeds only, shortest possible exposure, good light |
| Iranian video depends on another person | Testing for Iran is delayed | Give a written filming guide and agree dates early |
| Public dataset licences | Data cannot be used commercially | Check each licence before training |
| Trial plate models | Their licences may not allow commercial use | Use them for trials only. Train our own models before a customer pilot |
| Personal data rules | Footage or storage may be restricted | Blur public material, keep data private, take legal advice before a pilot |

## 10. Open decisions

- Licence for the public source code. It is published without a licence for now (6 November 2026), so all rights stay reserved: people can read the code but not reuse it.
- Confirmation of the three proposed values in section 4.
