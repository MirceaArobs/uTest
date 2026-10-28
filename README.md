this program helps running automated tests on SOC boards.
using framebuffer mode improves the response to events
using QT library improves the user experience

the tests can use any SOC resources like ethernet, uart or gpio

the program will show a catalog with the available tests ( from existing C# test files)
the user will be able to add individual or entire test groups to the execution list
the user will be able to enable / disable any test from the execution list
when test execution is started all the enabled tests will be executed one after the other
during test execution the user will be able to stop it
the tests will be marked as passed or failed as soon as they finish execution
the user will be able to select in the executio list only the failed tests (this help in debugging them)
additional space in the GUI will be reserved for:
 a. test logs (shown in real time) 
 and 
 b. trace data flow view (an oscilloscope like view that shows the events and their corresponding timestamps); the events can be any of a gpio line toggle or a message sent or received via any communication port
 
 the program will save the test logs including the trace data and summary of a test execution (that will group the info of all tests executed in that batch)

 The main window shows the test catalog and active-items list side by side; drag catalog entries to the active list to add them. A single row of controls sits immediately above the rolling 30-second UART/GPIO event timeline. A crosshair follows the mouse over the graph to make event markers easier to target. Demo event buttons add sample events; labels are shortened to two characters, and selecting a marker shows its elapsed timestamp and complete details in the status line at the bottom of the window. **Pause scroll** freezes the display while capture continues. **Stop capture** pauses scrolling and ignores incoming events; while stopped, use Older/Newer or drag the graph horizontally to browse captured history, and use Zoom -/Zoom + or the mouse wheel to change the displayed time range. Channel handlers should pass received data to `EventTimeline::addEvent(channel, details)` (for example, from a UART receive callback or GPIO edge handler). Demo buttons do not open device ports or configure GPIO hardware.
 
 HowTo:
 
 I. BUILD:
  1. cmake -S . -B build
   (The Configuration Step) - this command tells CMake to look at your project settings and generate the platform-specific build files (like Makefiles on Linux/macOS or Visual Studio solutions on Windows). It does not compile your code.
    -S . (Source): Specifies where the source code lives. The dot (.) means "the current directory". CMake will look for a CMakeLists.txt file here.
    -B build (Build): Specifies the target directory where CMake should put all generated build files, object files, and final binaries. If the build/ folder doesn't exist, CMake will automatically create it.

  2. cmake --build build
    (The Compilation Step) - this command tells CMake to actually compile and link your source code to create the final executables or libraries.
    --build build: Directs CMake to enter the build/ directory and invoke whatever underlying build tool your operating system uses (such as make, ninja, or msbuild).
  
 II. RUN:
  1. programName -platform linuxfb
   or
  2. programName -platform eglfs (hardware accelerated)
   or
  3. programName -platform vnc:size=800x480 
   Notes: 
    a vnc server will be started and a port will be shown at program start 
    connect to it from any computer on the same network with a vnc client / viewer (ipofrpi:port the port is usually 5900 but is displayed when the program starts)
  
