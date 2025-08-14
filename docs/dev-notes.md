# Implementation Notes for Ultra-Light Game Screen Recorder

## Project Overview
The Ultra-Light Game Screen Recorder is designed to provide a low-memory and low-CPU solution for capturing game footage. It supports both low-end and high-end capture methods, audio/mic capture, and includes DRM/account validation to ensure secure usage.

## Folder Structure
- **app/**: Contains the user interface and authentication components.
- **core/**: Implements the core functionality for capturing, encoding, and I/O operations.
- **server/**: Manages the license and validation services.
- **docs/**: Holds documentation and development notes.
- **libs/**: Contains optional third-party libraries for low-level tasks.

## Key Components
1. **Authentication**: 
   - User login via email/password or product key.
   - Token caching and hardware ID binding for security.

2. **Capture Methods**:
   - **Low-End**: Utilizes GDI functions (BitBlt/StretchBlt) for screen capture.
   - **High-End**: Hooks into GPU APIs (D3D9, DXGI, OpenGL) for more efficient capture.

3. **Encoding**:
   - Implements MJPEG encoding with options for delta-tiles to optimize performance during high motion.

4. **Audio Capture**:
   - Captures system audio and microphone input using WASAPI.

5. **I/O Management**:
   - Efficient disk writing with preallocated buffers and AVI container handling.

6. **Performance Optimization**:
   - Uses lock-free ring buffers for data transfer between components.
   - Implements fallback logic to adjust frame rates based on system performance.

## Development Workflow
- Follow the task list outlined in `tasks/tasks.yaml` for structured implementation.
- Ensure thorough testing of each component, focusing on performance metrics and stability.
- Document any changes or updates in this file to maintain clarity in the development process.

## Diagrams
- Flowcharts illustrating the capture and encoding pipeline.
- Sequence diagrams for user authentication and token validation processes.

## Future Enhancements
- Explore additional encoding formats for broader compatibility.
- Consider implementing cloud storage options for recorded footage.
- Investigate further optimizations for low-end systems to enhance usability.

## Conclusion
This document serves as a guide for the development of the Ultra-Light Game Screen Recorder, outlining the key components, workflow, and future directions for the project.