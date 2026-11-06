"""Compile the production power gesture state machine against timed input frames."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.cpp').read_text()
constants = source.split('constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS', 1)[1].split('}  // namespace', 1)[0]
function = 'bool handlePowerHighlight(' + source.split('bool handlePowerHighlight(', 1)[1].split('// Bluetooth page turner', 1)[0]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
unsigned long now = 1;
unsigned long millis() { return now; }
struct HalGPIO { static constexpr int BTN_POWER = 0; };
struct Input {
 bool held=false, down=false, up=false; unsigned long duration=0;
 bool isPressed(int) { return held; }
 bool wasPressed(int) { return down; }
 bool wasReleased(int) { return up; }
 unsigned long getPowerButtonHeldTime() { return duration; }
} gpio;
struct Settings { bool doubleClickPwrLight=false; } SETTINGS;
struct BoardConfig { static bool isX4Pro() { return true; } };
struct Manager {
 uint32_t visitId=1; int captures=0;
 uint32_t visit() { return visitId; }
 void capturePowerHighlight() { ++captures; }
} activityManager;
unsigned long lastX4ProPowerClickAt=0;
int light=0;
void toggleFrontlight() { ++light; }
'''
harness += 'constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS' + constants + function
harness += r'''
bool frame(unsigned long time, bool held, bool down, bool up, unsigned long duration, bool supported=true, bool other=false) {
 now=time; gpio={held,down,up,duration}; return handlePowerHighlight(supported,other);
}
void reset(bool doubleClick=false) {
 powerHighlightGesture=PowerHighlightGesture::Idle; activityManager={}; gpio={};
 SETTINGS.doubleClickPwrLight=doubleClick; light=0; powerHighlightClickAt=0;
}
int main() {
 reset(); frame(1,true,true,false,0); assert(frame(101,false,false,true,100));
 assert(activityManager.captures==1 && light==0);
 reset(); frame(1,true,true,false,0); assert(frame(501,false,false,true,500));
 assert(activityManager.captures==0);
 reset(true); frame(1,true,true,false,0); frame(101,false,false,true,100);
 assert(activityManager.captures==0); frame(600,false,false,false,0); assert(activityManager.captures==0);
 frame(602,false,false,false,0); assert(activityManager.captures==1);
 reset(true); frame(1,true,true,false,0); frame(101,false,false,true,100);
 frame(201,true,true,false,0); frame(301,false,false,true,100);
 frame(1000,false,false,false,0); assert(activityManager.captures==0 && light==1);
 reset(true); frame(1,true,true,false,0); frame(101,false,false,true,100);
 frame(200,false,false,false,0,true,true); frame(1000,false,false,false,0);
 assert(activityManager.captures==0);
 reset(true); frame(1,true,true,false,0); frame(101,false,false,true,100);
 ++activityManager.visitId; frame(1000,false,false,false,0);
 assert(activityManager.captures==0);
 reset(true); frame(1,true,true,false,0); frame(101,false,false,true,100);
 frame(200,false,false,false,0,false); frame(1000,false,false,false,0);
 assert(activityManager.captures==0);
 reset(); frame(1,true,true,false,0); frame(50,true,false,false,50,true,true);
 assert(frame(101,false,false,true,100)); assert(activityManager.captures==0);
 reset(); assert(!frame(1,false,false,true,100,false)); assert(activityManager.captures==0);
 puts("Power highlight gesture checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='crosspoint-power-') as directory:
    cpp = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    cpp.write_text(harness)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
