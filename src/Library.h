#pragma once

#include "ILuauModule.h"
#include "ILuauHost.h"
#include "lua.h"

namespace Luwow::Process {
    using ILuauModule = Luwow::Engine::ILuauModule;
    using ILuauHost = Luwow::Engine::ILuauHost;
    using RunMode = Luwow::Engine::RunMode;

    class Library : public ILuauModule {
    public:
        Library() = default;
        ~Library() override = default;

        ILuauModule* initialize(ILuauHost* host) override;

        const char* getModuleName() const override;
        const char* getModuleAlias() const override;
        const LuauExport* getExports() const override;

        RunMode getRunMode() const override;
        ILuauHost* getHost() const { return host; }

    private:
        ILuauHost* host = nullptr;
    };
} // namespace Luwow::Process
