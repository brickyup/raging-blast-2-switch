// Raging Blast 2 (Switch, native p2 renderer): the same game hooks as the desktop build.
#include "generated/default/rb2_init.h"

#include "rb2_hooks.h"
#include "rb2_switch_app.h"

#ifdef __SWITCH__
// Loaders that start the NRO without argv[0] would leave the SDK without the executable folder
// (game/ and rb2.toml live there). libnx sets __system_argv before static constructors run.
extern "C" int __system_argc;
extern "C" char** __system_argv;
namespace {
const bool kArgvByDefault = [] {
  static char path[] = "sdmc:/switch/rb2-nx/rb2.nro";
  static char* argv[] = {path, nullptr};
  if (__system_argc <= 0 || !__system_argv || !__system_argv[0] || !__system_argv[0][0]) {
    __system_argc = 1;
    __system_argv = argv;
  }
  return true;
}();
}  // namespace
#endif

REX_DEFINE_APP(rb2, Rb2SwitchApp::Create)
