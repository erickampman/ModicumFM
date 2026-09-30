// pm_module_test — exercises the vendored PM module core (Module + ModuleMap) compiling and
// running off Apple: construct a Module from a param group, read/write params, run its
// process proc, reset, and place it in a ModuleMap. Prints "ALL PASS".

#include "Module.hpp"
#include "ModuleMap.hpp"
#include "pm_params.h"

#include <cstdio>

static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
   do {                                                                                            \
      if (!(cond)) {                                                                               \
         std::printf("FAIL: %s\n", (msg));                                                         \
         ++g_fails;                                                                                \
      }                                                                                            \
   } while (0)

// Trivial process proc: fill output with the module's "Level" param (item 1).
static const KPBuffPair* testProc(uint64_t, uint32_t count, Module* m) {
   const float v = m->getParamValue(1);
   for (uint32_t i = 0; i < count; ++i) {
      m->output.first->data[i] = v;
      m->output.second->data[i] = v;
   }
   return &m->output;
}

int main() {
   static const ModuleParamConstants items[] = {
      // name    item unit flags valueStrings min  max  def  info
      {"Level", 1, 0, 0, nullptr, 0.0f, 1.0f, 0.5f, 0},
   };
   static const ModuleParamConstantsGroup group = {const_cast<char*>("test"), 1, items};

   const AUParameterAddress id = MODULE_PARAM_ADDRESS(ModuleParamKindLevel, ModuleInstance1, 0);

   Module m(id, 48000.0, 512, &group, nullptr, nullptr, testProc, nullptr, nullptr, nullptr,
            nullptr);
   m.addParams(&group);

   CHECK(m.getParamValue(1) == 0.5f, "default param from group");
   CHECK(m.moduleKind() == ModuleParamKindLevel, "moduleKind decodes from address");

   // Set a param the way Voice::setParameter does (direct write, 0-based index).
   m.params[0] = 0.25f;
   CHECK(m.getParamValue(1) == 0.25f, "param write/read");

   // Run the process proc.
   m.processProc(1, 16, &m);
   CHECK(m.output.first->data[0] == 0.25f && m.output.second->data[15] == 0.25f, "process output");

   // reset() clears output buffers.
   m.reset();
   CHECK(m.output.first->data[0] == 0.0f, "reset clears output");

   // ModuleMap: O(1) [kind][instance] lookup.
   {
      ModuleMap map;
      map.reserveModules(1);
      auto mod = std::make_unique<Module>(id, 48000.0, 512, &group);
      Module* raw = mod.get();
      map.add(id, std::move(mod));
      CHECK(map.get(id) == raw, "ModuleMap add/get");
      CHECK(map.allModules().size() == 1, "ModuleMap iteration");
   }

   if (g_fails == 0)
      std::printf("ALL PASS\n");
   return g_fails ? 1 : 0;
}
