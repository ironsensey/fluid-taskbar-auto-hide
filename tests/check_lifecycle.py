"""Compile the actual combined lifecycle against stubs, not the Windows hooks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'fluid-taskbar-auto-hide.wh.cpp').read_text()
footer = source[source.rindex('\nnamespace fluid {'):]
stubs = r'''
#include <cassert>
#include <string>
#include <vector>
#define WINAPI
using BOOL = int;
using HMODULE = void*;
using HANDLE = void*;
using LPCWSTR = const wchar_t*;
using DWORD = unsigned long;
constexpr BOOL TRUE = 1, FALSE = 0;
std::vector<std::string> calls;
bool hookOk = true, visibilityOk = true, animationOk = true;
HMODULE LoadLibraryExW(LPCWSTR, HANDLE, DWORD) { return reinterpret_cast<void*>(1); }
HMODULE GetModuleHandleW(LPCWSTR) { return reinterpret_cast<void*>(1); }
void* GetProcAddress(HMODULE, const char*) {
    return reinterpret_cast<void*>(&LoadLibraryExW);
}
namespace WindhawkUtils {
template<class T> bool Wh_SetFunctionHookT(T target, T, T* original) {
    calls.push_back("hook");
    *original = target;
    return hookOk;
}
}
namespace click_lock {
bool startOk = true, started = false;
bool Start() { calls.push_back("click.start"); started = startOk; return startOk; }
void Stop() { if (started) calls.push_back("click.stop"); started = false; }
}
namespace visibility {
BOOL Initialize() { calls.push_back("v.init"); return visibilityOk; }
void AfterInitialize() { calls.push_back("v.after"); }
void Uninitialize() { calls.push_back("v.uninit"); }
void HandleLoadedModuleIfExplorerPatcher(HMODULE) { calls.push_back("v.ep"); }
void HandleLoadedModuleIfTaskbarView(HMODULE, LPCWSTR) { calls.push_back("v.view"); }
}
namespace animation {
BOOL Initialize() { calls.push_back("a.init"); return animationOk; }
void AfterInitialize() { calls.push_back("a.after"); }
void Uninitialize() { calls.push_back("a.uninit"); }
void HandleLoadedModuleIfExplorerPatcher(HMODULE) { calls.push_back("a.ep"); }
void HandleLoadedModuleIfTaskbarView(HMODULE, LPCWSTR) { calls.push_back("a.view"); }
}
'''
tests = r'''
int main() {
    assert(Wh_ModInit());
    assert((calls == std::vector<std::string>{"hook", "v.init", "a.init", "click.start"}));
    calls.clear();
    Wh_ModAfterInit();
    assert((calls == std::vector<std::string>{"v.after", "a.after"}));
    calls.clear();
    assert(fluid::LoadLibraryExW_Hook(L"test", nullptr, 0));
    assert((calls == std::vector<std::string>{"v.ep", "v.view", "a.ep", "a.view"}));
    calls.clear();
    BOOL reload = FALSE;
    assert(Wh_ModSettingsChanged(&reload) && reload);
    Wh_ModUninit();
    Wh_ModUninit();
    assert((calls == std::vector<std::string>{"click.stop", "a.uninit", "v.uninit"}));
    calls.clear();
    animationOk = false;
    assert(!Wh_ModInit());
    Wh_ModUninit();
    assert((calls == std::vector<std::string>{"hook", "v.init", "a.init", "v.uninit"}));
    calls.clear();
    visibilityOk = false;
    assert(!Wh_ModInit());
    assert((calls == std::vector<std::string>{"hook", "v.init"}));
    calls.clear();
    hookOk = false;
    assert(!Wh_ModInit());
    assert((calls == std::vector<std::string>{"hook"}));
    calls.clear();
    hookOk = visibilityOk = animationOk = true;
    click_lock::startOk = false;
    assert(!Wh_ModInit());
    Wh_ModUninit();
    assert((calls == std::vector<std::string>{"hook", "v.init", "a.init", "click.start", "a.uninit", "v.uninit"}));
}
'''
with tempfile.TemporaryDirectory(prefix='fluid-lifecycle-') as temp:
    cpp = Path(temp) / 'lifecycle.cpp'
    binary = Path(temp) / 'lifecycle'
    cpp.write_text(stubs + footer + tests)
    subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-fsanitize=address,undefined', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Combined lifecycle, shared loader dispatch and failure cleanup passed (stub host)')
