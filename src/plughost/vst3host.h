#pragma once

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/vsttypes.h>
#include <public.sdk/source/vst/hosting/hostclasses.h>
#include <public.sdk/source/vst/hosting/module.h>
#include <public.sdk/source/vst/hosting/plugprovider.h>

namespace wb {

class VST3ComponentHandler : public Steinberg::Vst::IComponentHandler {
  public:
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override {
        //SMTG_DBPRT1("beginEdit called (%d)\n", id);
        return Steinberg::kNotImplemented;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue valueNormalized) override {
        //SMTG_DBPRT2("performEdit called (%d, %f)\n", id, valueNormalized);
        return Steinberg::kNotImplemented;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override {
        //SMTG_DBPRT1("endEdit called (%d)\n", id);
        return Steinberg::kNotImplemented;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override {
        //SMTG_DBPRT1("restartComponent called (%d)\n", flags);
        return Steinberg::kNotImplemented;
    }

  private:
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*_iid*/, void** /*obj*/) override {
        return Steinberg::kNoInterface;
    }
    // we do not care here of the ref-counting. A plug-in call of release should not destroy this
    // class!
    Steinberg::uint32 PLUGIN_API addRef() override { return 1000; }
    Steinberg::uint32 PLUGIN_API release() override { return 1000; }
};

struct VST3Host {
    VST3::Hosting::Module::Ptr module_;
    std::vector<VST3::Hosting::ClassInfo> class_infos_;
    Steinberg::IPtr<Steinberg::Vst::PlugProvider> plug_provider_;
    Steinberg::OPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::OPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::Vst::HostApplication plugin_context_;
    VST3ComponentHandler component_handler_;

    Steinberg::IPtr<Steinberg::IPlugView> view;

    VST3Host();
    bool open_module(const std::string& path);
    bool init_view();
};
} // namespace wb