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
 
