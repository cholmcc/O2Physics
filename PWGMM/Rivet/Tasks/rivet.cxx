// Copyright 2023-2099 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
//
// #include <Framework/AnalysisTask.h>
#include <Framework/AnalysisHelpers.h>
#include <Framework/AnalysisTask.h>
#include <Framework/DeviceSpec.h>
#include <Framework/RunningWorkflowInfo.h>
#include <Framework/DataProcessingDevice.h>
#include <Generators/AODToHepMC.h>
#include "Wrapper.h"

//==================================================================
/**
 * Check if two values are close, where close is defined by the
 * relative and absolute tolerances.
 *
 * @f$ |a - b| \le (\epsilon_a + \epsilon_r |b|) @f$
 *
 * Suppose the absolute tolerance is zero @f$\epsilon_a=0@f$ ,
 * then the above is equivalent to
 *
 * @f$ \frac{|a-b|}{|b|} \le \epsilon_r@f$
 *
 * If the relative tolerance is zero @f$\epsilon_r=0@f$, then we
 * have
 *
 * @f$ |a - b| \leg \epsilon_a @f$
 *
 * If both are non-zero, the one takes care of small numbers
 * (absolute tolerance) while the other takes care of larger
 * numbers.
 *
 * @param a "new" value
 * @param b "reference" value
 * @param atol Absolute tolerrance @f$\epsilon_a@f$
 * @param rtol Absolute tolerrance @f$\epsilon_r@f$
 *
 * @return true if the condition is met.
 *
 * @ingroup utils
 */
template <typename T>
bool isclose(T a, T b, T rtol = 1e-5, T atol = 1e-8)
{
  return std::abs(a - b) <= (atol + rtol * std::abs(b));
}

//____________________________________________________________________
template <typename T>
using OutputObj = o2::framework::OutputObj<T>;

using ConfigParamSpec = o2::framework::ConfigParamSpec;
template <typename T>
using Configurable = o2::framework::Configurable<T>;

//--------------------------------------------------------------------
/** A DPL to process simulation output (@c o2::aod::MCCollision and @c
 *  o2::aod::McParticles) in Rivet analyses.
 *
 *  The code uses the service classes @c o2::eventgen::AODToHepMC and
 *  @c o2::rivet::Wrapper to fulfill this task.
 *
 *  The output (result of the analyses) is stored in a @c
 *  o2::rivet::RivetAOs object.  This object essentially contain a
 *  string containing the YODA objects generated by the Rivet
 *  Analyses.  Therefore, to get the output of the Rivet analyses,
 *  one need to retrieve the object from the output file
 *
 *  @verbatim
 *  $ root -l O2Physics/PWGMM/Rivet/macros/GetRivetAOs.macro\(\"AnalysisResults.root\"\)
 *  @endverbatim
 *
 *  which will write the YODA analysis objects to @c AnalysisResults.yoda
 *
 *  Alternatively one can pass the option @c --rivet-dump
 *  Rivet.yoda to get the (partial) output written to disk.
 *
 *  One can then process the YODA file as per normal Rivet
 *  analyses, for example in Python
 *
 *  @code
 *  from yoda import read
 *  from matplotlib.pyplot import gca, ion
 *
 *  ion()
 *  aos  = read('Rivet.yoda')
 *  hist = aos['/ALICE_YYYY_I1234567/d01-x01-y01']
 *  ax   = gca()
 *  ax.errorbar(hist.xMids(),hist.yVals(),hist.yErrs())
 *  @endcode
 *
 * If the processing of the input is split over many jobs (e.g., on
 * the Grid), then the resulting `AnalysisResults.root` files can be
 * merged
 *
 * @verbatim
 * hadd Merged.root AnalysisResults_1.root AnalysisResults_2.root ...
 * @endverbatim
 *
 * and the @c RivetAOs will be merged and the `terminate` step of the
 * Rivet analysis performed.
 *
 * The option `--hepmc-no-aux` disables passing of HepMC auxiliary
 * tables (Cross-section, PDF information, and Heavy-ion header).
 *
 * The thing to remember here, is that each task process is expected
 * to do a _complete_ job.  That is, a process _cannot_ assume that
 * another process has been called before-hand or will be called
 * later, for the same event in the same order.
 *
 * That is, each process will get _all_ events of a time-frame and
 * then the next process will get _all_ events of the time-frame.
 *
 * Processed do not process events piece-meal, but rather in whole.
 */
struct MmRivet {
  using Converter = o2::eventgen::AODToHepMC;
  using Wrapper = o2::rivet::Wrapper;
  using RivetAOsPtr = typename Wrapper::RivetAOsPtr;

  /** Our converter */
  Converter mConverter;
  /** Our wrapper */
  Wrapper mWrapper;
  /** Our output wrapped */
  OutputObj<o2::rivet::RivetAOs> mOutput{o2::rivet::RivetAOs()};

  /** @{
   *  @name Container types */
  using Headers = Converter::Headers;
  using Header = Converter::Header;
  using Tracks = Converter::Tracks;
  using XSections = Converter::XSections;
  using XSection = Converter::XSection;
  using PdfInfos = Converter::PdfInfos;
  using PdfInfo = Converter::PdfInfo;
  using HeavyIons = Converter::HeavyIons;
  using HeavyIon = Converter::HeavyIon;
  /** @} */

  /** Get the device name suffix from device name */
  auto get_device_name_suffix(const std::string& devname)
  {
    static auto typen = o2::framework::type_name<MmRivet>();
    static auto thisName = o2::framework::type_to_task_name(typen);
    LOG(info) << std::quoted(devname) << " vs " << std::quoted(thisName);
    bool self = devname.starts_with(thisName);
    if (not self) { // NOLINT
      return std::string();
    }

    return devname.substr(thisName.length());
  }

  /** Update a boolean configurable from a ConfigParamSpec if not
   * equal to `def`. */
  void update_bool(const ConfigParamSpec& option,
                   Configurable<bool>& config,
                   bool def = false)
  {
    bool val = option.defaultValue.get<bool>();
    if (val != def) {
      config.value = val;
    }
  }
  /** Update a string configurable if set in ConfigParamSpec.  This
   * will append to the current value using hte specified separator.
   */
  void update_str(const ConfigParamSpec& option,
                  Configurable<std::string>& config,
                  char sep = ',')
  {
    std::string val = option.defaultValue.get<std::string>();
    if (!val.empty()) { // Why can I not use the keyword `not` - so
                        // darn stupid!
      std::string old = config.value;
      if (!old.empty()) { // Why can I not use the keyword `not` -
                          // so darn stupid!
        old += sep;
      }
      old += val;
      LOG(info) << "Setting " << std::quoted(config.name) << " to "
                << std::quoted(config.value);
      config.value = old;
    }
  }

  /** Absorb other workflow options or make this a zombie */
  void absorb_or_die(o2::framework::InitContext& initContext)
  {
    // Begin merging of rivet tasks
    using DeviceSpec = o2::framework::DeviceSpec;
    using RunningWorkflowInfo = o2::framework::RunningWorkflowInfo;

    // Get other workflows
    auto& workflows = initContext.services().get<RunningWorkflowInfo const>();

    std::map<std::string, DeviceSpec*> rivets;
    // Loop over workflow devices
    for (const DeviceSpec& device : workflows.devices) {
      auto suf = get_device_name_suffix(device.name);

      // Check if workflow device name matches this
      if (suf.empty()) {
        continue;
      }
      rivets[suf] = const_cast<DeviceSpec*>(&device);
    }

    if (rivets.size() <= 1) {
      // Only one workflow, return immediately - nothing to do
      return;
    }

    // get own suffix. Is there a better way?
    auto& deviceSpec = initContext.services().get<DeviceSpec const>();
    // The suffix of this device.
    std::string suffix = get_device_name_suffix(deviceSpec.name);

    // Check if we're the first workflow.  Note, since we use a sorted
    // map, we can be confidient that the sorting will be the same in
    // all workflow programs.
    bool first = rivets.begin()->first == suffix;
    bool zombie = !first; // Why can I not use the keyword `not` - so
                          // darn stupid!

    if (zombie) {
      // Zero the analysis
      mWrapper.configs.anas.value = "";
      return;
    }

    // Current log level, may be -1 if not set
    int logLevel = mWrapper.findLogLevel(mWrapper.configs.log);

    // Loop over other device specs and check for consistent values of
    // options.  Note that we cannot change the settings of the other
    // devices here - they live in a different process and changes
    // done in this process are not propagated there.  Hence, we have
    // to fail hard if some setting isn't consistent.
    for (auto sufdevice : rivets) {
      std::string suf = sufdevice.first;
      auto& dev = *sufdevice.second;

      bool self = suf == suffix;
      if (self) {
        continue;
      }

      // Loop over the defined options
      for (auto& option : dev.options) {
        // LOG(info) << "  option " << std::quoted(option.name);
        // Check if we have cross-section
        if (option.name == mWrapper.configs.crossSection.name) {
          // Get the value specified, if any
          auto val = option.defaultValue.get<double>();
          if (val > 0) {
            // Why can I not use the keyword `not` - so darn stupid!
            if (!isclose(val, mWrapper.configs.crossSection.value)) {
              LOG(fatal) << "Inconsistent cross-section settings for Rivet: "
                         << val << " versus "
                         << mWrapper.configs.crossSection;
            } else {
              mWrapper.configs.crossSection.value = val;
            }
          }
        } // Do not mess with the formatting
        // Check if we have merge equivalent
        if (option.name == mWrapper.configs.mergeEquiv.name) { // No
          auto val = option.defaultValue.get<bool>();
          if (val != mWrapper.configs.mergeEquiv) {
            LOG(fatal) << "Inconsistent merge-equivilant settings for Rivet: "
                       << val << " versus " << mWrapper.configs.mergeEquiv;
          }
        } // Do not mess with the formatting
        // Check if we should recenter event
        if (option.name == mConverter.configs.recenter.name) {
          auto val = option.defaultValue.get<bool>();
          if (val != mConverter.configs.recenter) {
            LOG(fatal) << "Inconsistent setting for HepMC event recentering: "
                       << val << " versus "
                       << mConverter.configs.recenter;
          }
        } // Do not mess with the formatting
        // Check if we should only do generated events
        if (option.name == mConverter.configs.onlyGen.name) {
          auto val = option.defaultValue.get<bool>();
          if (val != mConverter.configs.onlyGen) {
            LOG(fatal) << "Inconsistent only-generated HepMC settings: "
                       << val << " versus "
                       << mConverter.configs.onlyGen;
          }
        } // Do not mess with the formatting
        // Check if we have ignore beams
        if (option.name == mWrapper.configs.ignoreBeams.name) {
          update_bool(option, mWrapper.configs.ignoreBeams);
        }
        // Check if we have pwd
        if (option.name == mWrapper.configs.pwd.name) {
          update_bool(option, mWrapper.configs.pwd);
        } // Do not mess with the formatting
        // Check if we have finalize
        if (option.name == mWrapper.configs.finalize.name) {
          update_bool(option, mWrapper.configs.finalize);
        } // Do not mess with the formatting
        // Check if we have analyses
        if (option.name == mWrapper.configs.anas.name) {
          update_str(option, mWrapper.configs.anas);
          option.defaultValue = "";
        } // Do not mess with the formatting
        // Check if we have paths
        if (option.name == mWrapper.configs.paths.name) {
          update_str(option, mWrapper.configs.paths, ':');
        } // Do not mess with the formatting
        // Check if we have preloads
        if (option.name == mWrapper.configs.pres.name) {
          update_str(option, mWrapper.configs.pres);
        } // Do not mess with the formatting
        // Check if we have sources
        if (option.name == mWrapper.configs.srcs.name) {
          update_str(option, mWrapper.configs.srcs);
        } // Do not mess with the formatting
        // Check if we have flags
        if (option.name == mWrapper.configs.flags.name) {
          update_str(option, mWrapper.configs.flags);
        } // Do not mess with the formatting
        // Check if we have logging flag
        if (option.name == mWrapper.configs.log.name) {
          int otherLvl = mWrapper.findLogLevel(option.       //
                                               defaultValue. //
                                               get<std::string>());
          if (otherLvl >= 0) {
            if (otherLvl < logLevel || logLevel < 0) { // I want to use `or`!
              logLevel = otherLvl;
              mWrapper.configs.log.value = //
                option.defaultValue.get<std::string>();
            }
          }
        }
      }
    }
    // End merging of rivet tasks
  }

  /** Initialize the job */
  void
    init(o2::framework::InitContext& initContext)
  {
    // using DeviceConfigurationHelpers = o2::framework::DeviceConfigurationHelpers;

    // According to Jan-Fiete: this is only needed when subwagons are used,
    // so probably not at all relevant, but we leave it in for now.

    // auto& options = initContext.options();
    // LOG(info) << " This analysis: "
    //           << std::quoted(options.get<std::string>("rivet-analysis"));
    absorb_or_die(initContext);

    mConverter.init();
    mWrapper.init(mOutput.object);
  }
  /** Process events */
  void process(Header const& collision,
               XSections const& xsections,
               PdfInfos const& pdfs,
               HeavyIons const& heavyions,
               Tracks const& tracks)
  {
    // In case we were passed the option `--hepmc-no-aux`, then do no
    // processing anything here, as it will double count the event.
    // The issue is, that each time frame is sent to each process of
    // the task independent of one another.  That means this process
    // will get _all_ events one at a time, and only when that is done
    // will the events be passed to other process member functions,
    // again one event at a time.  Thus, we cannot rely on the events
    // being _distributed_ to the processes at the same time.
    if (doPlain) {
      return;
    }
    if (mWrapper.mAnalyses.size() <= 0) {
      // If we can exit the process gracefully, we should really do
      // that here.
      LOG(warning) << "No analysis registered!";
      return;
    }
    LOG(info) << "=== Processing all information";
    // assert(xsections.size() == 1);
    // assert(heavyions.size() == 1);
    // assert(pdfs.size() == 1);

    mConverter.startEvent();
    mConverter.process(collision,  // Prevent
                       xsections,  // clang-format
                       pdfs,       // from putting this
                       heavyions); // on one big line
    mConverter.process(collision, tracks);
    mConverter.endEvent();

    mWrapper.process(mConverter.mEvent);
  }
  /** Process input */
  void processPlain(Header const& collision,
                    Tracks const& tracks)
  {
    // If we're also asked to process the auxiliary information, then
    // do nothing here, as that will mess up the processing.
    if (!doPlain) {
      return;
    }
    if (mWrapper.mAnalyses.size() <= 0) {
      LOG(warning) << "No analysis registered!";
      return;
    }

    LOG(info) << "=== Processing tracks and header information";

    mConverter.startEvent();
    mConverter.process(collision, tracks);
    mConverter.endEvent();

    mWrapper.process(mConverter.mEvent);
  }
  decltype(o2::framework::ProcessConfigurable{&MmRivet::processPlain,
                                              "hepmx-no-aux", false,
                                              "Do not process Auxiliary info"})
    doPlain = o2::framework::ProcessConfigurable{&MmRivet::processPlain,
                                                 "hepmc-no-aux", false,
                                                 "Do not process "
                                                 "Auxillary info"};
};

//--------------------------------------------------------------------
// This _must_ be included after our "customize" function above, or
// that function will not be taken into account.
#include <Framework/runDataProcessing.h>

//--------------------------------------------------------------------
using WorkflowSpec = o2::framework::WorkflowSpec;
using TaskName = o2::framework::TaskName;
using ConfigContext = o2::framework::ConfigContext;

/** Entry point of @a o2-analysis-mm-rivet */
WorkflowSpec defineDataProcessing(ConfigContext const& cfg)
{
  using o2::framework::adaptAnalysisTask;

  // Task: Two entry: header, tracks, and header, tracks, auxiliary
  return WorkflowSpec{
    adaptAnalysisTask<MmRivet>(cfg)};
}
//
// EOF
//
