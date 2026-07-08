#include "SpektraParameters.h"
#include "SpektraProfileCurves.h"
#include "SpektraRenderer.h"
#if defined __APPLE__
#  include "SpektraMetalRenderer.h"
#endif
#include "SpektraTooltips.h"

#include "ofxImageEffect.h"
#include "ofxColour.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxGPURender.h"

#if defined __APPLE__
#  include <ApplicationServices/ApplicationServices.h>
#  include <dlfcn.h>
#elif defined _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#elif defined __linux__
#  include <dlfcn.h>
#  include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#if defined __APPLE__
#  define SPEKTRA_EXPORT __attribute__((visibility("default")))
#elif defined _WIN32
#  define SPEKTRA_EXPORT __declspec(dllexport)
#elif defined __linux__
#  define SPEKTRA_EXPORT __attribute__((visibility("default")))
#else
#  define SPEKTRA_EXPORT
#endif

#ifndef kOfxBitDepthHalf
#  define kOfxBitDepthHalf "OfxBitDepthHalf"
#endif

#ifndef kOfxParamTypePage
#  define kOfxParamTypePage "OfxParamTypePage"
#endif

#ifndef kOfxParamPropPageChild
#  define kOfxParamPropPageChild "OfxParamPropPageChild"
#endif

#ifndef kOfxPluginPropParamPageOrder
#  define kOfxPluginPropParamPageOrder "OfxPluginPropParamPageOrder"
#endif

#ifndef kOfxParamStringIsLabel
#  define kOfxParamStringIsLabel "OfxParamStringIsLabel"
#endif

#ifndef SPEKTRAFILM_VERSION_STRING
#  define SPEKTRAFILM_VERSION_STRING "0.2.1"
#endif

#ifndef SPEKTRAFILM_PRODUCT_VERSION_STRING
#  define SPEKTRAFILM_PRODUCT_VERSION_STRING SPEKTRAFILM_VERSION_STRING
#endif

#ifndef SPEKTRAFILM_PRODUCT_VERSION_MAJOR
#  define SPEKTRAFILM_PRODUCT_VERSION_MAJOR 0
#endif

#ifndef SPEKTRAFILM_PRODUCT_VERSION_MINOR
#  define SPEKTRAFILM_PRODUCT_VERSION_MINOR 2
#endif

#ifndef SPEKTRAFILM_PRODUCT_VERSION_PATCH
#  define SPEKTRAFILM_PRODUCT_VERSION_PATCH 0
#endif

#ifndef SPEKTRAFILM_PLUGIN_IDENTIFIER
#  define SPEKTRAFILM_PLUGIN_IDENTIFIER "org.spektrafilm.dev"
#endif

#ifndef SPEKTRAFILM_PLUGIN_LABEL
#  define SPEKTRAFILM_PLUGIN_LABEL "spektrafilm dev"
#endif

#ifndef SPEKTRAFILM_PLUGIN_FLAVOR
#  define SPEKTRAFILM_PLUGIN_FLAVOR 2
#endif

#ifndef SPEKTRAFILM_PRO_BUILD_MODE
#  define SPEKTRAFILM_PRO_BUILD_MODE 0
#endif

#ifndef SPEKTRAFILM_PRODUCT_KIND
#  define SPEKTRAFILM_PRODUCT_KIND 0
#endif

#ifndef SPEKTRAFILM_OFX_METAL_GPU_BUFFERS
#  define SPEKTRAFILM_OFX_METAL_GPU_BUFFERS 0
#endif

namespace {

constexpr const char *kPluginIdentifier = SPEKTRAFILM_PLUGIN_IDENTIFIER;
constexpr const char *kPluginLabel = SPEKTRAFILM_PLUGIN_LABEL;
constexpr int kPluginVersionMajor = SPEKTRAFILM_PRODUCT_VERSION_MAJOR;
constexpr int kPluginVersionMinor = SPEKTRAFILM_PRODUCT_VERSION_MINOR;

OfxHost *gHost = nullptr;
OfxImageEffectSuiteV1 *gEffectHost = nullptr;
OfxPropertySuiteV1 *gPropHost = nullptr;
OfxParameterSuiteV1 *gParamHost = nullptr;
OfxMessageSuiteV1 *gMessageHost = nullptr;
int gPluginImageAnchor = 0;

enum class PluginFlavor : int32_t {
  Flow = 0,
  Pro = 1,
  FilmDev = 2,
};

constexpr PluginFlavor kPluginFlavor = static_cast<PluginFlavor>(SPEKTRAFILM_PLUGIN_FLAVOR);

enum class ProBuildMode : int32_t {
  Production = 0,
  Calibration = 1,
};

constexpr ProBuildMode kProBuildMode = static_cast<ProBuildMode>(SPEKTRAFILM_PRO_BUILD_MODE);

enum class ProductKind : int32_t {
  None = 0,
  Cine = 1,
  Photo = 2,
  Scan = 3,
};

constexpr ProductKind kProductKind = static_cast<ProductKind>(SPEKTRAFILM_PRODUCT_KIND);

constexpr bool isProProductionBuild() {
  return kPluginFlavor == PluginFlavor::Pro && kProBuildMode == ProBuildMode::Production;
}

constexpr bool isProCalibrationBuild() {
  return kPluginFlavor == PluginFlavor::Pro && kProBuildMode == ProBuildMode::Calibration;
}

constexpr bool isProductCine() {
  return isProProductionBuild() && kProductKind == ProductKind::Cine;
}

constexpr bool isProductPhoto() {
  return isProProductionBuild() && kProductKind == ProductKind::Photo;
}

constexpr bool isProductScan() {
  return isProProductionBuild() && kProductKind == ProductKind::Scan;
}

enum ParamTag : uint32_t {
  kParamTagNone = 0u,
  kParamTagFlow = 1u << 0u,
  kParamTagDevelopment = 1u << 1u,
};

struct ParamMetadata {
  const char *name;
  const char *parentGroup;
  uint32_t tags;
};

constexpr uint32_t flow() {
  return kParamTagFlow;
}

constexpr uint32_t development() {
  return kParamTagDevelopment;
}

constexpr uint32_t flowDevelopment() {
  return kParamTagFlow | kParamTagDevelopment;
}

inline constexpr ParamMetadata kParamMetadata[] = {
  {"process", "colorGroup", flow()},
  {"scanNegativeInvert", "colorGroup", flow()},
  {"inputColorSpace", "colorGroup", flow()},
  {"inputPrimariesColorSpace", "colorGroup", flow()},
  {"inputTransferColorSpace", "colorGroup", flow()},
  {"rcmInputColorSpace", "colorGroup", flow()},
  {"outputRole", "colorGroup", flow()},
  {"sdrOutputColorSpace", "colorGroup", flow()},
  {"outputPrimariesColorSpace", "colorGroup", flow()},
  {"outputTransferColorSpace", "colorGroup", flow()},
  {"sceneOutputColorSpace", "colorGroup", kParamTagDevelopment},
  {"hdrPreset", "colorGroup", flow()},
  {"hdrTransfer", "colorGroup", flow()},
  {"hdrReferenceWhiteNits", "colorGroup", flow()},
  {"hdrPeakNits", "colorGroup", flow()},
  {"hdrExposureEv", "colorGroup", flow()},
  {"hdrToneMapping", "colorGroup", flow()},
  {"colorAdaptation", "colorGroup", flow()},
  {"colorAdaptationInputCompression", "colorGroup", kParamTagNone},
  {"colorAdaptationCurveSmoothing", "colorGroup", kParamTagNone},
  {"colorAdaptationOutputLightnessCompression", "colorGroup", kParamTagNone},
  {"colorAdaptationOutputChromaCompression", "colorGroup", kParamTagNone},

  {"cameraUvFilterEnabled", "filteringGroup", kParamTagNone},
  {"cameraUvCutNm", "filteringGroup", kParamTagNone},
  {"cameraIrFilterEnabled", "filteringGroup", kParamTagNone},
  {"cameraIrCutNm", "filteringGroup", kParamTagNone},

  {"productionProfileNegative", "productionStocksGroup", kParamTagNone},
  {"productionProfilePrint", "productionStocksGroup", kParamTagNone},

  {"rgbToRawMethod", "filmGroup", flow()},
  {"film", "filmGroup", flow()},
  {"filmFormat", "filmGroup", flow()},
  {"filmPushPullMode", "filmGroup", flow()},
  {"filmPushPullStops", "filmGroup", flow()},
  {"negativeBleachBypassAmount", "filmGroup", flowDevelopment()},
  {"negativeLeucoCyanCoupling", "filmGroup", development()},
  {"filmExposureEv", "filmGroup", flow()},
  {"autoExposure", "filmGroup", kParamTagNone},
  {"autoExposureMethod", "filmGroup", kParamTagNone},
  {"filmGamma", "filmGroup", development()},

  {"printSource", "printSourceGroup", kParamTagNone},
  {"paper", "printGroup", flow()},
  {"printTiming", "printGroup", flow()},
  {"scanInputEncoding", "filmScanGroup", kParamTagNone},
  {"scanInputColorSpace", "filmScanGroup", kParamTagNone},
  {"scanWorkingColorSpace", "filmScanGroup", kParamTagNone},
  {"scanDensityBasis", "filmScanGroup", kParamTagNone},
  {"scanFilmBaseRgb", "filmScanGroup", kParamTagNone},
  {"scanFilmBaseColorRgb", "filmScanGroup", kParamTagNone},
  {"scanFilmBaseTemp", "filmScanGroup", kParamTagNone},
  {"scanFilmBaseTint", "filmScanGroup", kParamTagNone},
  {"scanBlackFlareRgb", "filmScanGroup", kParamTagNone},
  {"scanExposureEv", "filmScanGroup", kParamTagNone},
  {"scanDensityContrast", "filmScanGroup", kParamTagNone},
  {"scanDensityScaleRgb", "filmScanGroup", kParamTagNone},
  {"scanDensityScaleR", "filmScanGroup", kParamTagNone},
  {"scanDensityScaleG", "filmScanGroup", kParamTagNone},
  {"scanDensityScaleB", "filmScanGroup", kParamTagNone},
  {"scanDensityOffsetRgb", "filmScanGroup", kParamTagNone},
  {"printPushPullStops", "printGroup", flow()},
  {"printBleachBypassAmount", "printGroup", flowDevelopment()},
  {"printExposureEv", "printGroup", flow()},
  {"printGamma", "printGroup", development()},
  {"printShadowShape", "printGroup", flow()},
  {"printHighlightShape", "printGroup", flow()},
  {"filterC", "printGroup", flow()},
  {"filterMShift", "printGroup", flow()},
  {"filterYShift", "printGroup", flow()},
  {"preflashExposure", "printGroup", flow()},
  {"preflashMFilterShift", "printGroup", flow()},
  {"preflashYFilterShift", "printGroup", flow()},
  {"productionPrinterLightsEnabled", "productionLaboratoryGroup", kParamTagNone},
  {"productionPrinterLightsLinked", "productionLaboratoryGroup", kParamTagNone},
  {"creativePrinterLightR", "productionLaboratoryGroup", kParamTagNone},
  {"creativePrinterLightG", "productionLaboratoryGroup", kParamTagNone},
  {"creativePrinterLightB", "productionLaboratoryGroup", kParamTagNone},
  {"printerLightsGang", "printGroup", flow()},
  {"printerLightsGroup", "printGroup", flow()},
  {"printerLightR", "printGroup", flow()},
  {"printerLightG", "printGroup", flow()},
  {"printerLightB", "printGroup", flow()},
  {"printerLightCalibration", "printGroup", kParamTagNone},

  {"enlargerScale", "enlargerGroup", kParamTagNone},
  {"enlargerOffsetXPercent", "enlargerGroup", kParamTagNone},
  {"enlargerOffsetYPercent", "enlargerGroup", kParamTagNone},

  {"dirAmount", "couplerGroup", flow()},
  {"dirDiffusionUm", "couplerGroup", flow()},
  {"dirDiffusionTailUm", "couplerGroup", kParamTagNone},
  {"dirDiffusionTailWeight", "couplerGroup", kParamTagNone},
  {"dirInhibitionSameLayer", "couplerGroup", flow()},
  {"dirInhibitionInterlayer", "couplerGroup", flow()},
  {"dirGammaSameLayerRgb", "couplerGroup", kParamTagNone},
  {"dirGammaRToGb", "couplerGroup", kParamTagNone},
  {"dirGammaGToRb", "couplerGroup", kParamTagNone},
  {"dirGammaBToRg", "couplerGroup", kParamTagNone},
  {"dirCalibrateToStock", "couplerGroup", kParamTagNone},

  {"grainEnabled", "grainGroup", flow()},
  {"grainModel", "grainGroup", flow()},
  {"grainAmount", "grainGroup", flow()},
  {"grainSaturation", "grainGroup", flow()},
  {"grainSublayersEnabled", "grainGroup", kParamTagNone},
  {"grainSubLayerCount", "grainGroup", kParamTagNone},
  {"grainParticleAreaUm2", "grainGroup", flow()},
  {"grainParticleScale", "grainGroup", kParamTagNone},
  {"grainParticleScaleLayers", "grainGroup", kParamTagNone},
  {"grainDensityMin", "grainGroup", kParamTagNone},
  {"grainUniformity", "grainGroup", kParamTagNone},
  {"grainFinalBlurUm", "grainGroup", kParamTagNone},
  {"grainBlurDyeCloudsUm", "grainGroup", kParamTagNone},
  {"grainMicroStructure", "grainGroup", kParamTagNone},
  {"grainSeed", "grainGroup", kParamTagNone},
  {"grainAnimate", "grainGroup", kParamTagNone},
  {"grainSynthesisSize", "grainGroup", development()},
  {"grainSynthesisAmount", "grainGroup", development()},
  {"grainSynthesisSharpness", "grainGroup", development()},
  {"grainSynthesisQuality", "grainGroup", development()},

  {"grainSynthesisSamples", "grainSynthesisGroup", development()},
  {"grainSynthesisMeanRadiusUm", "grainSynthesisGroup", development()},
  {"grainSynthesisRadiusStdDevRatio", "grainSynthesisGroup", development()},
  {"grainSynthesisObservationSigmaUm", "grainSynthesisGroup", development()},
  {"grainSynthesisCellSizeRatio", "grainSynthesisGroup", development()},
  {"grainSynthesisMaxRadiusQuantile", "grainSynthesisGroup", development()},
  {"grainSynthesisCoverageEpsilon", "grainSynthesisGroup", development()},
  {"grainSynthesisMaxGrainsPerCell", "grainSynthesisGroup", development()},
  {"grainSynthesisRadiusScale", "grainSynthesisGroup", development()},
  {"grainSynthesisLayerScale", "grainSynthesisGroup", development()},
  {"grainSynthesisLayered", "grainSynthesisGroup", development()},

  {"halationEnabled", "halationGroup", flow()},
  {"scatterAmount", "halationGroup", kParamTagNone},
  {"scatterScale", "halationGroup", kParamTagNone},
  {"halationAmount", "halationGroup", flow()},
  {"halationScale", "halationGroup", flow()},
  {"halationBoostEv", "halationGroup", flow()},
  {"halationBoostRange", "halationGroup", kParamTagNone},
  {"halationProtectEv", "halationGroup", kParamTagNone},
  {"halationStrength", "halationGroup", flow()},

  {"cameraDiffusionEnabled", "diffusionGroup", flow()},
  {"cameraDiffusionFamily", "diffusionGroup", flow()},
  {"cameraDiffusionStrength", "diffusionGroup", flow()},
  {"cameraDiffusionSpatialScale", "diffusionGroup", kParamTagNone},
  {"cameraDiffusionHaloWarmth", "diffusionGroup", flow()},
  {"cameraDiffusionCoreIntensity", "diffusionGroup", kParamTagNone},
  {"cameraDiffusionCoreSize", "diffusionGroup", kParamTagNone},
  {"cameraDiffusionHaloIntensity", "diffusionGroup", kParamTagNone},
  {"cameraDiffusionHaloSize", "diffusionGroup", kParamTagNone},
  {"cameraDiffusionBloomIntensity", "diffusionGroup", kParamTagNone},
  {"cameraDiffusionBloomSize", "diffusionGroup", kParamTagNone},
  {"printDiffusionEnabled", "diffusionGroup", flow()},
  {"printDiffusionFamily", "diffusionGroup", flow()},
  {"printDiffusionStrength", "diffusionGroup", flow()},
  {"printDiffusionSpatialScale", "diffusionGroup", kParamTagNone},
  {"printDiffusionHaloWarmth", "diffusionGroup", flow()},
  {"printDiffusionCoreIntensity", "diffusionGroup", kParamTagNone},
  {"printDiffusionCoreSize", "diffusionGroup", kParamTagNone},
  {"printDiffusionHaloIntensity", "diffusionGroup", kParamTagNone},
  {"printDiffusionHaloSize", "diffusionGroup", kParamTagNone},
  {"printDiffusionBloomIntensity", "diffusionGroup", kParamTagNone},
  {"printDiffusionBloomSize", "diffusionGroup", kParamTagNone},

  {"scannerEnabled", "scannerGroup", flow()},
  {"scannerWhiteCorrection", "scannerGroup", flow()},
  {"scannerBlackCorrection", "scannerGroup", flow()},
  {"scannerWhiteLevel", "scannerGroup", flow()},
  {"scannerBlackLevel", "scannerGroup", flow()},
  {"glarePercent", "scannerGroup", kParamTagNone},
  {"glareRoughness", "scannerGroup", kParamTagNone},
  {"glareBlur", "scannerGroup", kParamTagNone},
  {"scannerMtf50LpMm", "scannerGroup", kParamTagNone},
  {"scannerUnsharpRadiusUm", "scannerGroup", kParamTagNone},
  {"scannerUnsharpAmount", "scannerGroup", kParamTagNone},

  {"gpuRenderTiling", "manageGroup", kParamTagNone},

  {"infoVersion", "infoGroup", flow()},
  {"infoCreatedBy", "infoGroup", flow()},
  {"infoBasedOn", "infoGroup", flow()},

  {"supportAuthorAndrea", "supportGroup", kParamTagNone},
  {"supportAuthorAedan", "supportGroup", kParamTagNone},
  {"supportAuthorMagno", "supportGroup", kParamTagNone},
  {"supportAuthorPH", "supportGroup", kParamTagNone},
  {"supportAboutHelp", "supportGroup", kParamTagNone},
  {"supportOpenMCNexus", "supportGroup", kParamTagNone},

  {"calibrationBuildInfo", "calibrationGroup", kParamTagNone},
  {"activeCalibrationInfo", "calibrationGroup", kParamTagNone},
  {"hostColourManagementInfo", "calibrationGroup", kParamTagNone},
  {"colourManagementConfigInfo", "calibrationGroup", kParamTagNone},
  {"ocioConfigInfo", "calibrationGroup", kParamTagNone},
  {"sourceColourspaceInfo", "calibrationGroup", kParamTagNone},
  {"outputColourspaceInfo", "calibrationGroup", kParamTagNone},
  {"displayColourspaceInfo", "calibrationGroup", kParamTagNone},
  {"resolvedUserTimelineInfo", "calibrationGroup", kParamTagNone},
  {"hostClipPreferencesCallInfo", "calibrationGroup", kParamTagNone},
  {"hostOutputColourspaceCallInfo", "calibrationGroup", kParamTagNone},
  {"hostClipPreferenceRequestInfo", "calibrationGroup", kParamTagNone},
  {"hostOutputPreferredRequestInfo", "calibrationGroup", kParamTagNone},
  {"hostOutputColourspaceReplyInfo", "calibrationGroup", kParamTagNone},
  {"hostClipPreferenceMode", "calibrationGroup", kParamTagNone},
  {"hostOutputColourspaceMode", "calibrationGroup", kParamTagNone},
  {"refreshHostColourDiagnostics", "calibrationGroup", kParamTagNone},
  {"saveGlobalCalibration", "calibrationGroup", kParamTagNone},
  {"saveNegativeCalibration", "calibrationGroup", kParamTagNone},
  {"savePrintCalibration", "calibrationGroup", kParamTagNone},
  {"savePairCalibration", "calibrationGroup", kParamTagNone},
  {"exportProductionCalibration", "calibrationGroup", kParamTagNone},
  {"loadActiveCalibration", "calibrationGroup", kParamTagNone},
};

const ParamMetadata *metadataForParam(const char *name) {
  for (const ParamMetadata &metadata : kParamMetadata) {
    if (std::strcmp(metadata.name, name) == 0) {
      return &metadata;
    }
  }
  return nullptr;
}

bool stringInList(const char *name, const char *const *items, size_t count) {
  if (!name) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(name, items[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool productionPublicParam(const char *name) {
  constexpr const char *kProductionCommonParams[] = {
    "inputPrimariesColorSpace",
    "inputTransferColorSpace",
    "outputPrimariesColorSpace",
    "outputTransferColorSpace",
    "supportAboutHelp",
    "supportOpenMCNexus",
  };
  constexpr const char *kProductionCineParams[] = {
    "filmFormat",
    "filmExposureEv",
    "productionProfileNegative",
    "productionProfilePrint",
    "filmPushPullStops",
    "negativeBleachBypassAmount",
    "productionPrinterLightsEnabled",
    "productionPrinterLightsLinked",
    "creativePrinterLightR",
    "creativePrinterLightG",
    "creativePrinterLightB",
    "printBleachBypassAmount",
  };
  constexpr const char *kProductionPhotoParams[] = {
    "filmFormat",
    "filmExposureEv",
    "productionProfileNegative",
    "productionProfilePrint",
    "filmPushPullStops",
    "negativeBleachBypassAmount",
    "printPushPullStops",
    "printBleachBypassAmount",
  };
  constexpr const char *kProductionScanParams[] = {
    "scanFilmBaseColorRgb",
    "scanFilmBaseTemp",
    "scanFilmBaseTint",
    "scanExposureEv",
    "scanDensityScaleR",
    "scanDensityScaleG",
    "scanDensityScaleB",
    "productionProfilePrint",
    "printPushPullStops",
    "printBleachBypassAmount",
  };
  if (stringInList(name, kProductionCommonParams, std::size(kProductionCommonParams))) {
    return true;
  }
  if (isProductPhoto()) {
    return stringInList(name, kProductionPhotoParams, std::size(kProductionPhotoParams));
  }
  if (isProductScan()) {
    return stringInList(name, kProductionScanParams, std::size(kProductionScanParams));
  }
  return stringInList(name, kProductionCineParams, std::size(kProductionCineParams));
}

bool productionInternalHiddenParam(const char *name) {
  constexpr const char *kProductionInternalHiddenParams[] = {
    "dirUsesStockCalibration",
  };
  return stringInList(name, kProductionInternalHiddenParams, std::size(kProductionInternalHiddenParams));
}

bool calibrationParam(const char *name) {
  constexpr const char *kCalibrationParams[] = {
    "cameraUvFilterEnabled",
    "cameraUvCutNm",
    "cameraIrFilterEnabled",
    "cameraIrCutNm",
    "rgbToRawMethod",
    "filmGamma",
    "negativeLeucoCyanCoupling",
    "printExposureEv",
    "printGamma",
    "printShadowShape",
    "printHighlightShape",
    "filterC",
    "filterMShift",
    "filterYShift",
    "preflashExposure",
    "preflashMFilterShift",
    "preflashYFilterShift",
    "dirAmount",
    "dirDiffusionUm",
    "dirDiffusionTailUm",
    "dirDiffusionTailWeight",
    "dirInhibitionSameLayer",
    "dirInhibitionInterlayer",
    "dirGammaSameLayerRgb",
    "dirGammaRToGb",
    "dirGammaGToRb",
    "dirGammaBToRg",
    "dirUsesStockCalibration",
  };
  return stringInList(name, kCalibrationParams, std::size(kCalibrationParams));
}

bool parameterVisibleInFlavor(const ParamMetadata &metadata) {
  if (isProProductionBuild()) {
    return productionPublicParam(metadata.name);
  }
  if (isProCalibrationBuild()) {
    if (std::strncmp(metadata.name, "creativePrinterLight", 20) == 0 ||
        std::strncmp(metadata.name, "productionProfile", 17) == 0 ||
        std::strncmp(metadata.name, "productionPrinterLight", 22) == 0) {
      return false;
    }
    return true;
  }
  const bool flowTagged = (metadata.tags & kParamTagFlow) != 0u;
  const bool developmentTagged = (metadata.tags & kParamTagDevelopment) != 0u;
  if (kPluginFlavor == PluginFlavor::FilmDev) {
    return true;
  }
  if (kPluginFlavor == PluginFlavor::Pro) {
    return !developmentTagged;
  }
  return flowTagged && !developmentTagged;
}

bool shouldDefineParam(const char *name) {
  if (isProProductionBuild()) {
    return productionPublicParam(name) || productionInternalHiddenParam(name);
  }
  return true;
}

bool groupVisibleInFlavor(const char *name) {
  if (isProProductionBuild()) {
    if (std::strcmp(name, "colorGroup") == 0 ||
        std::strcmp(name, "productionStocksGroup") == 0 ||
        std::strcmp(name, "productionLaboratoryGroup") == 0 ||
        std::strcmp(name, "supportGroup") == 0) {
      return true;
    }
    if (!isProductScan() && std::strcmp(name, "productionCameraGroup") == 0) {
      return true;
    }
    return isProductScan() && std::strcmp(name, "scannedNegativeGroup") == 0;
  }
  if (isProCalibrationBuild()) {
    return
      std::strcmp(name, "scannedNegativeGroup") != 0 &&
      std::strcmp(name, "productionCameraGroup") != 0 &&
      std::strcmp(name, "productionStocksGroup") != 0 &&
      std::strcmp(name, "productionLaboratoryGroup") != 0;
  }
  if (std::strcmp(name, "manageGroup") == 0) {
    return true;
  }
  if (kPluginFlavor == PluginFlavor::FilmDev) {
    return true;
  }
  for (const ParamMetadata &metadata : kParamMetadata) {
    if (std::strcmp(metadata.parentGroup, name) == 0 && parameterVisibleInFlavor(metadata)) {
      return true;
    }
  }
  return false;
}

bool shouldDefineGroup(const char *name) {
  if (isProProductionBuild() || isProCalibrationBuild()) {
    return groupVisibleInFlavor(name);
  }
  return true;
}

bool parameterVisibleInFlavor(const char *name) {
  if (isProProductionBuild()) {
    return productionPublicParam(name);
  }
  const ParamMetadata *metadata = metadataForParam(name);
  return !metadata || parameterVisibleInFlavor(*metadata);
}

bool parameterHiddenInFlavor(const char *name) {
  return !parameterVisibleInFlavor(name);
}

constexpr bool flavorAllowsDevelopmentControls() {
  return kPluginFlavor == PluginFlavor::FilmDev;
}

int grainModelOptionCountForFlavor() {
  return flavorAllowsDevelopmentControls() ? 3 : 2;
}

constexpr int outputRoleOptionCountForFlavor() {
  return 3;
}

spektrafilm::OutputRole outputRoleForFlavor(int value) {
  switch (static_cast<spektrafilm::OutputRole>(value)) {
    case spektrafilm::OutputRole::DisplayHdr:
      return spektrafilm::OutputRole::DisplayHdr;
    case spektrafilm::OutputRole::Rcm:
      return spektrafilm::OutputRole::Rcm;
    case spektrafilm::OutputRole::DisplaySdr:
    default:
      return spektrafilm::OutputRole::DisplaySdr;
  }
}

enum class ParamValueKind : uint8_t {
  Int = 1,
  Bool = 2,
  Double = 3,
  Double2D = 4,
  Double3D = 5,
};

struct ParamDefault {
  const char *name;
  ParamValueKind kind;
  int intDefault;
  double doubleDefault[3];
};

constexpr ParamDefault intDefault(const char *name, int value) {
  return {name, ParamValueKind::Int, value, {0.0, 0.0, 0.0}};
}

constexpr ParamDefault boolDefault(const char *name, bool value) {
  return {name, ParamValueKind::Bool, value ? 1 : 0, {0.0, 0.0, 0.0}};
}

constexpr ParamDefault doubleDefault(const char *name, double value) {
  return {name, ParamValueKind::Double, 0, {value, 0.0, 0.0}};
}

constexpr ParamDefault double2DDefault(const char *name, double x, double y) {
  return {name, ParamValueKind::Double2D, 0, {x, y, 0.0}};
}

constexpr ParamDefault double3DDefault(const char *name, double x, double y, double z) {
  return {name, ParamValueKind::Double3D, 0, {x, y, z}};
}

constexpr const char *kGrainSeedParamName = "grainSeed";
constexpr int kGrainSeedMin = 0;
constexpr int kGrainSeedMax = 1000000;

inline constexpr ParamDefault kParamDefaults[] = {
  intDefault("process", 0),
  boolDefault("scanNegativeInvert", false),
  intDefault("inputColorSpace", 1),
  intDefault("inputPrimariesColorSpace", 1),
  intDefault("inputTransferColorSpace", 1),
  intDefault("rcmInputColorSpace", 0),
  intDefault("outputRole", 0),
  intDefault("sdrOutputColorSpace", 8),
  intDefault("outputPrimariesColorSpace", 12),
  intDefault("outputTransferColorSpace", 17),
  intDefault("sceneOutputColorSpace", 0),
  intDefault("hostClipPreferenceMode", 0),
  intDefault("hostOutputColourspaceMode", 0),
  intDefault("hdrPreset", 0),
  intDefault("hdrTransfer", 0),
  doubleDefault("hdrReferenceWhiteNits", 203.0),
  doubleDefault("hdrPeakNits", 1000.0),
  doubleDefault("hdrExposureEv", 0.0),
  intDefault("hdrToneMapping", 1),
  boolDefault("colorAdaptation", false),
  boolDefault("colorAdaptationInputCompression", true),
  boolDefault("colorAdaptationCurveSmoothing", true),
  boolDefault("colorAdaptationOutputLightnessCompression", true),
  boolDefault("colorAdaptationOutputChromaCompression", true),

  boolDefault("cameraUvFilterEnabled", false),
  doubleDefault("cameraUvCutNm", 410.0),
  boolDefault("cameraIrFilterEnabled", false),
  doubleDefault("cameraIrCutNm", 675.0),

  intDefault("productionProfileNegative", 0),
  intDefault("productionProfilePrint", 0),

  intDefault("rgbToRawMethod", 0),
  intDefault("film", static_cast<int>(spektrafilm::kSpektraDefaultFilmIndex)),
  intDefault("filmPushPullMode", 0),
  doubleDefault("filmPushPullStops", 0.0),
  doubleDefault("negativeBleachBypassAmount", 0.0),
  doubleDefault("negativeLeucoCyanCoupling", 1.0),
  doubleDefault("filmExposureEv", 0.0),
  boolDefault("autoExposure", false),
  intDefault("autoExposureMethod", 0),
  doubleDefault("filmGamma", 1.0),

  intDefault("paper", static_cast<int>(spektrafilm::kSpektraDefaultPaperIndex)),
  intDefault("printTiming", 0),
  intDefault("printSource", 0),
  intDefault("scanInputEncoding", 0),
  intDefault("scanInputColorSpace", 0),
  intDefault("scanWorkingColorSpace", 0),
  intDefault("scanDensityBasis", 0),
  double3DDefault("scanFilmBaseRgb", 1.0, 1.0, 1.0),
  double3DDefault("scanFilmBaseColorRgb", 1.0, 0.78, 0.58),
  doubleDefault("scanFilmBaseTemp", 0.0),
  doubleDefault("scanFilmBaseTint", 0.0),
  double3DDefault("scanBlackFlareRgb", 0.0, 0.0, 0.0),
  doubleDefault("scanExposureEv", 0.0),
  doubleDefault("scanDensityContrast", 1.5),
  double3DDefault("scanDensityScaleRgb", 1.0, 1.0, 1.0),
  doubleDefault("scanDensityScaleR", 1.0),
  doubleDefault("scanDensityScaleG", 1.0),
  doubleDefault("scanDensityScaleB", 1.0),
  double3DDefault("scanDensityOffsetRgb", 0.0, 0.0, 0.0),
  doubleDefault("printPushPullStops", 0.0),
  doubleDefault("printBleachBypassAmount", 0.0),
  doubleDefault("printExposureEv", 0.0),
  doubleDefault("printGamma", 1.0),
  doubleDefault("printShadowShape", 0.0),
  doubleDefault("printHighlightShape", 0.0),
  doubleDefault("filterC", 0.0),
  doubleDefault("filterMShift", 0.0),
  doubleDefault("filterYShift", 0.0),
  doubleDefault("preflashExposure", 0.0),
  doubleDefault("preflashMFilterShift", 0.0),
  doubleDefault("preflashYFilterShift", 0.0),
  boolDefault("productionPrinterLightsEnabled", true),
  boolDefault("productionPrinterLightsLinked", false),
  doubleDefault("creativePrinterLightR", 0.0),
  doubleDefault("creativePrinterLightG", 0.0),
  doubleDefault("creativePrinterLightB", 0.0),
  boolDefault("printerLightsGang", false),
  boolDefault("printerLightsGroup", false),
  doubleDefault("printerLightR", 0.0),
  doubleDefault("printerLightG", 0.0),
  doubleDefault("printerLightB", 0.0),
  boolDefault("printerLightCalibration", true),

  doubleDefault("enlargerScale", 1.0),
  doubleDefault("enlargerOffsetXPercent", 0.0),
  doubleDefault("enlargerOffsetYPercent", 0.0),

  doubleDefault("dirAmount", 0.0),
  doubleDefault("dirDiffusionUm", 20.0),
  doubleDefault("dirDiffusionTailUm", 200.0),
  doubleDefault("dirDiffusionTailWeight", 0.06),
  doubleDefault("dirInhibitionSameLayer", 1.0),
  doubleDefault("dirInhibitionInterlayer", 1.0),
  double3DDefault("dirGammaSameLayerRgb", 0.336, 0.319, 0.273),
  double2DDefault("dirGammaRToGb", 0.353, 0.302),
  double2DDefault("dirGammaGToRb", 0.154, 0.353),
  double2DDefault("dirGammaBToRg", 0.168, 0.226),
  boolDefault("dirUsesStockCalibration", true),

  boolDefault("grainEnabled", false),
  intDefault("grainModel", 0),
  intDefault("filmFormat", 4),
  doubleDefault("grainAmount", 1.0),
  doubleDefault("grainSaturation", 1.0),
  boolDefault("grainSublayersEnabled", true),
  intDefault("grainSubLayerCount", 1),
  doubleDefault("grainParticleAreaUm2", 0.1),
  double3DDefault("grainParticleScale", 1.2, 1.0, 2.5),
  double3DDefault("grainParticleScaleLayers", 6.0, 1.0, 0.4),
  double3DDefault("grainDensityMin", 0.04, 0.05, 0.06),
  double3DDefault("grainUniformity", 0.99, 0.97, 0.98),
  doubleDefault("grainFinalBlurUm", 7.17),
  doubleDefault("grainBlurDyeCloudsUm", 1.0),
  double2DDefault("grainMicroStructure", 0.2, 30.0),
  intDefault("grainSeed", 0),
  boolDefault("grainAnimate", true),
  doubleDefault("grainSynthesisSize", 1.0),
  doubleDefault("grainSynthesisAmount", 1.0),
  doubleDefault("grainSynthesisSharpness", 1.0),
  doubleDefault("grainSynthesisQuality", 1.0),

  intDefault("grainSynthesisSamples", 128),
  doubleDefault("grainSynthesisMeanRadiusUm", 0.25),
  doubleDefault("grainSynthesisRadiusStdDevRatio", 0.0),
  doubleDefault("grainSynthesisObservationSigmaUm", 1.0),
  doubleDefault("grainSynthesisCellSizeRatio", 1.0),
  doubleDefault("grainSynthesisMaxRadiusQuantile", 0.999),
  doubleDefault("grainSynthesisCoverageEpsilon", 0.0001),
  intDefault("grainSynthesisMaxGrainsPerCell", 32),
  double3DDefault("grainSynthesisRadiusScale", 1.2, 1.0, 2.5),
  double3DDefault("grainSynthesisLayerScale", 6.0, 1.0, 0.4),
  boolDefault("grainSynthesisLayered", true),

  boolDefault("halationEnabled", false),
  doubleDefault("scatterAmount", 1.0),
  doubleDefault("scatterScale", 1.0),
  doubleDefault("halationAmount", 1.0),
  doubleDefault("halationScale", 1.0),
  double3DDefault("halationStrength", 0.05, 0.015, 0.0),
  doubleDefault("halationBoostEv", 0.0),
  doubleDefault("halationBoostRange", 0.3),
  doubleDefault("halationProtectEv", 4.0),

  boolDefault("cameraDiffusionEnabled", false),
  intDefault("cameraDiffusionFamily", 1),
  doubleDefault("cameraDiffusionStrength", 0.5),
  doubleDefault("cameraDiffusionSpatialScale", 1.0),
  doubleDefault("cameraDiffusionHaloWarmth", 0.0),
  doubleDefault("cameraDiffusionCoreIntensity", 1.0),
  doubleDefault("cameraDiffusionCoreSize", 1.0),
  doubleDefault("cameraDiffusionHaloIntensity", 1.0),
  doubleDefault("cameraDiffusionHaloSize", 1.0),
  doubleDefault("cameraDiffusionBloomIntensity", 1.0),
  doubleDefault("cameraDiffusionBloomSize", 1.0),
  boolDefault("printDiffusionEnabled", false),
  intDefault("printDiffusionFamily", 1),
  doubleDefault("printDiffusionStrength", 0.5),
  doubleDefault("printDiffusionSpatialScale", 1.0),
  doubleDefault("printDiffusionHaloWarmth", 0.0),
  doubleDefault("printDiffusionCoreIntensity", 1.0),
  doubleDefault("printDiffusionCoreSize", 1.0),
  doubleDefault("printDiffusionHaloIntensity", 1.0),
  doubleDefault("printDiffusionHaloSize", 1.0),
  doubleDefault("printDiffusionBloomIntensity", 1.0),
  doubleDefault("printDiffusionBloomSize", 1.0),

  boolDefault("scannerEnabled", false),
  boolDefault("scannerWhiteCorrection", false),
  boolDefault("scannerBlackCorrection", false),
  doubleDefault("scannerWhiteLevel", 0.98),
  doubleDefault("scannerBlackLevel", 0.01),
  doubleDefault("glarePercent", 0.03),
  doubleDefault("glareRoughness", 0.7),
  doubleDefault("glareBlur", 0.5),
  doubleDefault("scannerMtf50LpMm", 60.0),
  doubleDefault("scannerUnsharpRadiusUm", 5.0),
  doubleDefault("scannerUnsharpAmount", 0.7),

  intDefault("gpuRenderTiling", 0),
};

struct StoredParamValue {
  ParamValueKind kind = ParamValueKind::Int;
  int intValue[3] = {0, 0, 0};
  double doubleValue[3] = {0.0, 0.0, 0.0};
};

using DefaultsSnapshot = std::unordered_map<std::string, StoredParamValue>;

enum class SnapshotScope {
  All,
  ProductionPublic,
  Calibration,
};

struct CalibrationSnapshot {
  DefaultsSnapshot global;
  DefaultsSnapshot negative;
  DefaultsSnapshot print;
  DefaultsSnapshot pairOverride;
};

const DefaultsSnapshot *gDescribeDefaults = nullptr;
int gDescriptorGrainSeedDefault = 0;

bool isGrainSeedParam(const char *name) {
  return name && std::strcmp(name, kGrainSeedParamName) == 0;
}

std::mt19937 makeGrainSeedRng() {
  uint32_t seedData[] = {
    static_cast<uint32_t>(std::time(nullptr)),
    static_cast<uint32_t>(std::clock()),
    0x9e3779b9u,
    0x85ebca6bu,
    0xc2b2ae35u,
    0x27d4eb2fu,
  };
  try {
    std::random_device device;
    for (uint32_t &word : seedData) {
      word ^= device();
    }
  } catch (...) {
  }
  std::seed_seq seed(std::begin(seedData), std::end(seedData));
  return std::mt19937(seed);
}

int randomGrainSeed() {
  static std::mutex mutex;
  static std::mt19937 rng = makeGrainSeedRng();
  std::lock_guard<std::mutex> lock(mutex);
  std::uniform_int_distribution<int> distribution(kGrainSeedMin, kGrainSeedMax);
  return distribution(rng);
}

int descriptorGrainSeedDefault() {
  gDescriptorGrainSeedDefault = randomGrainSeed();
  return gDescriptorGrainSeedDefault;
}

int paramComponentCount(ParamValueKind kind) {
  switch (kind) {
    case ParamValueKind::Double2D:
      return 2;
    case ParamValueKind::Double3D:
      return 3;
    case ParamValueKind::Int:
    case ParamValueKind::Bool:
    case ParamValueKind::Double:
    default:
      return 1;
  }
}

bool paramKindUsesDouble(ParamValueKind kind) {
  return kind == ParamValueKind::Double ||
    kind == ParamValueKind::Double2D ||
    kind == ParamValueKind::Double3D;
}

const ParamDefault *defaultForParam(const char *name) {
  for (const ParamDefault &entry : kParamDefaults) {
    if (std::strcmp(entry.name, name) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

StoredParamValue factoryStoredValue(const ParamDefault &entry) {
  StoredParamValue value{};
  value.kind = entry.kind;
  if (paramKindUsesDouble(entry.kind)) {
    for (int i = 0; i < paramComponentCount(entry.kind); ++i) {
      value.doubleValue[i] = entry.doubleDefault[i];
    }
  } else if (std::strcmp(entry.name, kGrainSeedParamName) == 0) {
    value.intValue[0] = randomGrainSeed();
  } else {
    value.intValue[0] = entry.intDefault;
  }
  return value;
}

bool storedValueForDefault(const char *name, StoredParamValue &value) {
  if (isGrainSeedParam(name)) {
    return false;
  }
  if (isProProductionBuild() && !productionPublicParam(name)) {
    return false;
  }
  if (!gDescribeDefaults) {
    return false;
  }
  const ParamDefault *entry = defaultForParam(name);
  if (!entry) {
    return false;
  }
  const auto found = gDescribeDefaults->find(name);
  if (found == gDescribeDefaults->end() || found->second.kind != entry->kind) {
    return false;
  }
  value = found->second;
  return true;
}

struct InstanceData {
  OfxImageClipHandle sourceClip = nullptr;
  OfxImageClipHandle outputClip = nullptr;

  OfxParamHandle filteringGroup = nullptr;
  OfxParamHandle enlargerGroup = nullptr;
  OfxParamHandle filmGroup = nullptr;
  OfxParamHandle printSourceGroup = nullptr;
  OfxParamHandle printGroup = nullptr;
  OfxParamHandle filmScanGroup = nullptr;
  OfxParamHandle scannedNegativeGroup = nullptr;
  OfxParamHandle couplerGroup = nullptr;
  OfxParamHandle calibrationGroup = nullptr;
  OfxParamHandle calibrationBuildInfo = nullptr;
  OfxParamHandle activeCalibrationInfo = nullptr;
  OfxParamHandle hostColourManagementInfo = nullptr;
  OfxParamHandle colourManagementConfigInfo = nullptr;
  OfxParamHandle ocioConfigInfo = nullptr;
  OfxParamHandle sourceColourspaceInfo = nullptr;
  OfxParamHandle outputColourspaceInfo = nullptr;
  OfxParamHandle displayColourspaceInfo = nullptr;
  OfxParamHandle resolvedUserTimelineInfo = nullptr;
  OfxParamHandle hostClipPreferencesCallInfo = nullptr;
  OfxParamHandle hostOutputColourspaceCallInfo = nullptr;
  OfxParamHandle hostClipPreferenceRequestInfo = nullptr;
  OfxParamHandle hostOutputPreferredRequestInfo = nullptr;
  OfxParamHandle hostOutputColourspaceReplyInfo = nullptr;
  OfxParamHandle hostClipPreferenceMode = nullptr;
  OfxParamHandle hostOutputColourspaceMode = nullptr;
  OfxParamHandle refreshHostColourDiagnostics = nullptr;
  int hostClipPreferencesCallCount = 0;
  int hostOutputColourspaceCallCount = 0;
  OfxParamHandle saveGlobalCalibration = nullptr;
  OfxParamHandle saveNegativeCalibration = nullptr;
  OfxParamHandle savePrintCalibration = nullptr;
  OfxParamHandle savePairCalibration = nullptr;
  OfxParamHandle exportProductionCalibration = nullptr;
  OfxParamHandle loadActiveCalibration = nullptr;
  OfxParamHandle productionCameraGroup = nullptr;
  OfxParamHandle productionStocksGroup = nullptr;
  OfxParamHandle productionLaboratoryGroup = nullptr;
  OfxParamHandle grainGroup = nullptr;
  OfxParamHandle grainSynthesisGroup = nullptr;
  OfxParamHandle halationGroup = nullptr;
  OfxParamHandle process = nullptr;
  OfxParamHandle scanNegativeInvert = nullptr;
  OfxParamHandle rgbToRawMethod = nullptr;
  OfxParamHandle inputColorSpace = nullptr;
  OfxParamHandle inputPrimariesColorSpace = nullptr;
  OfxParamHandle inputTransferColorSpace = nullptr;
  OfxParamHandle rcmInputColorSpace = nullptr;
  OfxParamHandle outputRole = nullptr;
  OfxParamHandle sdrOutputColorSpace = nullptr;
  OfxParamHandle outputPrimariesColorSpace = nullptr;
  OfxParamHandle outputTransferColorSpace = nullptr;
  OfxParamHandle sceneOutputColorSpace = nullptr;
  OfxParamHandle hdrPreset = nullptr;
  OfxParamHandle hdrTransfer = nullptr;
  OfxParamHandle hdrReferenceWhiteNits = nullptr;
  OfxParamHandle hdrPeakNits = nullptr;
  OfxParamHandle hdrExposureEv = nullptr;
  OfxParamHandle hdrToneMapping = nullptr;
  OfxParamHandle colorAdaptation = nullptr;
  OfxParamHandle colorAdaptationInputCompression = nullptr;
  OfxParamHandle colorAdaptationCurveSmoothing = nullptr;
  OfxParamHandle colorAdaptationOutputLightnessCompression = nullptr;
  OfxParamHandle colorAdaptationOutputChromaCompression = nullptr;
  OfxParamHandle cameraUvFilterEnabled = nullptr;
  OfxParamHandle cameraUvCutNm = nullptr;
  OfxParamHandle cameraIrFilterEnabled = nullptr;
  OfxParamHandle cameraIrCutNm = nullptr;
  OfxParamHandle productionProfileNegative = nullptr;
  OfxParamHandle productionProfilePrint = nullptr;
  OfxParamHandle film = nullptr;
  OfxParamHandle paper = nullptr;
  OfxParamHandle printTiming = nullptr;
  OfxParamHandle printSource = nullptr;
  OfxParamHandle scanInputEncoding = nullptr;
  OfxParamHandle scanInputColorSpace = nullptr;
  OfxParamHandle scanWorkingColorSpace = nullptr;
  OfxParamHandle scanDensityBasis = nullptr;
  OfxParamHandle scanFilmBaseRgb = nullptr;
  OfxParamHandle scanFilmBaseColorRgb = nullptr;
  OfxParamHandle scanFilmBaseTemp = nullptr;
  OfxParamHandle scanFilmBaseTint = nullptr;
  OfxParamHandle scanBlackFlareRgb = nullptr;
  OfxParamHandle scanExposureEv = nullptr;
  OfxParamHandle scanDensityContrast = nullptr;
  OfxParamHandle scanDensityScaleRgb = nullptr;
  OfxParamHandle scanDensityScaleR = nullptr;
  OfxParamHandle scanDensityScaleG = nullptr;
  OfxParamHandle scanDensityScaleB = nullptr;
  OfxParamHandle scanDensityOffsetRgb = nullptr;
  OfxParamHandle filmExposureEv = nullptr;
  OfxParamHandle autoExposure = nullptr;
  OfxParamHandle autoExposureMethod = nullptr;
  OfxParamHandle printExposureEv = nullptr;
  OfxParamHandle filmPushPullMode = nullptr;
  OfxParamHandle filmPushPullStops = nullptr;
  OfxParamHandle printPushPullStops = nullptr;
  OfxParamHandle negativeBleachBypassAmount = nullptr;
  OfxParamHandle negativeLeucoCyanCoupling = nullptr;
  OfxParamHandle printBleachBypassAmount = nullptr;
  OfxParamHandle filmGamma = nullptr;
  OfxParamHandle printGamma = nullptr;
  OfxParamHandle printShadowShape = nullptr;
  OfxParamHandle printHighlightShape = nullptr;
  OfxParamHandle filterC = nullptr;
  OfxParamHandle filterMShift = nullptr;
  OfxParamHandle filterYShift = nullptr;
  OfxParamHandle enlargerScale = nullptr;
  OfxParamHandle enlargerOffsetXPercent = nullptr;
  OfxParamHandle enlargerOffsetYPercent = nullptr;
  OfxParamHandle preflashExposure = nullptr;
  OfxParamHandle preflashMFilterShift = nullptr;
  OfxParamHandle preflashYFilterShift = nullptr;
  OfxParamHandle productionPrinterLightsEnabled = nullptr;
  OfxParamHandle productionPrinterLightsLinked = nullptr;
  OfxParamHandle creativePrinterLightR = nullptr;
  OfxParamHandle creativePrinterLightG = nullptr;
  OfxParamHandle creativePrinterLightB = nullptr;
  OfxParamHandle printerLightR = nullptr;
  OfxParamHandle printerLightG = nullptr;
  OfxParamHandle printerLightB = nullptr;
  OfxParamHandle printerLightsGang = nullptr;
  OfxParamHandle printerLightsGroup = nullptr;
  OfxParamHandle printerLightCalibration = nullptr;
  OfxParamHandle dirAmount = nullptr;
  OfxParamHandle dirDiffusionUm = nullptr;
  OfxParamHandle dirDiffusionTailUm = nullptr;
  OfxParamHandle dirDiffusionTailWeight = nullptr;
  OfxParamHandle dirInhibitionSameLayer = nullptr;
  OfxParamHandle dirInhibitionInterlayer = nullptr;
  OfxParamHandle dirGammaSameLayerRgb = nullptr;
  OfxParamHandle dirGammaRToGb = nullptr;
  OfxParamHandle dirGammaGToRb = nullptr;
  OfxParamHandle dirGammaBToRg = nullptr;
  OfxParamHandle dirCalibrateToStock = nullptr;
  OfxParamHandle dirUsesStockCalibration = nullptr;
  OfxParamHandle grainEnabled = nullptr;
  OfxParamHandle grainModel = nullptr;
  OfxParamHandle filmFormat = nullptr;
  OfxParamHandle grainAmount = nullptr;
  OfxParamHandle grainSaturation = nullptr;
  OfxParamHandle grainSublayersEnabled = nullptr;
  OfxParamHandle grainSubLayerCount = nullptr;
  OfxParamHandle grainParticleAreaUm2 = nullptr;
  OfxParamHandle grainParticleScale = nullptr;
  OfxParamHandle grainParticleScaleLayers = nullptr;
  OfxParamHandle grainDensityMin = nullptr;
  OfxParamHandle grainUniformity = nullptr;
  OfxParamHandle grainFinalBlurUm = nullptr;
  OfxParamHandle grainBlurDyeCloudsUm = nullptr;
  OfxParamHandle grainMicroStructure = nullptr;
  OfxParamHandle grainSeed = nullptr;
  OfxParamHandle grainAnimate = nullptr;
  OfxParamHandle grainSynthesisSize = nullptr;
  OfxParamHandle grainSynthesisAmount = nullptr;
  OfxParamHandle grainSynthesisSharpness = nullptr;
  OfxParamHandle grainSynthesisQuality = nullptr;
  OfxParamHandle grainSynthesisSamples = nullptr;
  OfxParamHandle grainSynthesisMeanRadiusUm = nullptr;
  OfxParamHandle grainSynthesisRadiusStdDevRatio = nullptr;
  OfxParamHandle grainSynthesisObservationSigmaUm = nullptr;
  OfxParamHandle grainSynthesisCellSizeRatio = nullptr;
  OfxParamHandle grainSynthesisMaxRadiusQuantile = nullptr;
  OfxParamHandle grainSynthesisCoverageEpsilon = nullptr;
  OfxParamHandle grainSynthesisMaxGrainsPerCell = nullptr;
  OfxParamHandle grainSynthesisRadiusScale = nullptr;
  OfxParamHandle grainSynthesisLayerScale = nullptr;
  OfxParamHandle grainSynthesisLayered = nullptr;
  OfxParamHandle halationEnabled = nullptr;
  OfxParamHandle scatterAmount = nullptr;
  OfxParamHandle scatterScale = nullptr;
  OfxParamHandle halationAmount = nullptr;
  OfxParamHandle halationScale = nullptr;
  OfxParamHandle halationStrength = nullptr;
  OfxParamHandle halationBoostEv = nullptr;
  OfxParamHandle halationBoostRange = nullptr;
  OfxParamHandle halationProtectEv = nullptr;
  OfxParamHandle cameraDiffusionEnabled = nullptr;
  OfxParamHandle cameraDiffusionFamily = nullptr;
  OfxParamHandle cameraDiffusionStrength = nullptr;
  OfxParamHandle cameraDiffusionSpatialScale = nullptr;
  OfxParamHandle cameraDiffusionHaloWarmth = nullptr;
  OfxParamHandle cameraDiffusionCoreIntensity = nullptr;
  OfxParamHandle cameraDiffusionCoreSize = nullptr;
  OfxParamHandle cameraDiffusionHaloIntensity = nullptr;
  OfxParamHandle cameraDiffusionHaloSize = nullptr;
  OfxParamHandle cameraDiffusionBloomIntensity = nullptr;
  OfxParamHandle cameraDiffusionBloomSize = nullptr;
  OfxParamHandle printDiffusionEnabled = nullptr;
  OfxParamHandle printDiffusionFamily = nullptr;
  OfxParamHandle printDiffusionStrength = nullptr;
  OfxParamHandle printDiffusionSpatialScale = nullptr;
  OfxParamHandle printDiffusionHaloWarmth = nullptr;
  OfxParamHandle printDiffusionCoreIntensity = nullptr;
  OfxParamHandle printDiffusionCoreSize = nullptr;
  OfxParamHandle printDiffusionHaloIntensity = nullptr;
  OfxParamHandle printDiffusionHaloSize = nullptr;
  OfxParamHandle printDiffusionBloomIntensity = nullptr;
  OfxParamHandle printDiffusionBloomSize = nullptr;
  OfxParamHandle scannerGroup = nullptr;
  OfxParamHandle scannerEnabled = nullptr;
  OfxParamHandle scannerWhiteCorrection = nullptr;
  OfxParamHandle scannerBlackCorrection = nullptr;
  OfxParamHandle scannerWhiteLevel = nullptr;
  OfxParamHandle scannerBlackLevel = nullptr;
  OfxParamHandle glarePercent = nullptr;
  OfxParamHandle glareRoughness = nullptr;
  OfxParamHandle glareBlur = nullptr;
  OfxParamHandle scannerMtf50LpMm = nullptr;
  OfxParamHandle scannerUnsharpRadiusUm = nullptr;
  OfxParamHandle scannerUnsharpAmount = nullptr;
  OfxParamHandle gpuRenderTiling = nullptr;
  OfxParamHandle lutSize = nullptr;
  OfxParamHandle lutDestination = nullptr;
  OfxParamHandle lutIdentifier = nullptr;
  OfxParamHandle exportLut = nullptr;
  OfxParamHandle presetName = nullptr;
  OfxParamHandle presetSelection = nullptr;
  OfxParamHandle savePreset = nullptr;
  OfxParamHandle loadPreset = nullptr;

  double lastPrinterLights[3] = {0.0, 0.0, 0.0};
  bool lastPrinterLightsInitialized = false;
  bool syncingPrinterLights = false;
  double lastCreativePrinterLights[3] = {0.0, 0.0, 0.0};
  bool lastCreativePrinterLightsInitialized = false;
  bool syncingCreativePrinterLights = false;
  bool syncingDirCalibration = false;
  bool metalGpuBufferProbeLogged = false;

  std::mutex rendererMutex;
  std::unique_ptr<spektrafilm::Renderer> renderer;
};

InstanceData *getInstanceData(OfxImageEffectHandle effect) {
  OfxPropertySetHandle props = nullptr;
  gEffectHost->getPropertySet(effect, &props);
  InstanceData *data = nullptr;
  gPropHost->propGetPointer(props, kOfxPropInstanceData, 0, reinterpret_cast<void **>(&data));
  return data;
}

spektrafilm::Renderer *ensureRenderer(InstanceData *data) {
  if (!data) {
    return nullptr;
  }
  if (!data->renderer) {
    data->renderer = spektrafilm::createNativeRenderer();
  }
  return data->renderer.get();
}

void releaseInstanceRendererResources(InstanceData *data, bool resetRenderer);

int mapPixelDepth(const char *depth) {
  if (!depth) {
    return 0;
  }
  if (std::strcmp(depth, kOfxBitDepthHalf) == 0) {
    return 16;
  }
  if (std::strcmp(depth, kOfxBitDepthFloat) == 0) {
    return 32;
  }
  return 0;
}

int componentsForString(const char *components) {
  if (!components) {
    return 0;
  }
  if (std::strcmp(components, kOfxImageComponentRGBA) == 0) {
    return 4;
  }
  if (std::strcmp(components, kOfxImageComponentRGB) == 0) {
    return 3;
  }
  if (std::strcmp(components, kOfxImageComponentAlpha) == 0) {
    return 1;
  }
  return 0;
}

OfxStatus fetchImageView(
  OfxImageClipHandle clip,
  OfxTime time,
  OfxPropertySetHandle *image,
  spektrafilm::ImageView &view
) {
  if (gEffectHost->clipGetImage(clip, time, nullptr, image) != kOfxStatOK || !*image) {
    return kOfxStatFailed;
  }

  OfxRectI bounds{};
  char *depth = nullptr;
  char *components = nullptr;
  void *data = nullptr;
  int rowBytes = 0;
  gPropHost->propGetIntN(*image, kOfxImagePropBounds, 4, &bounds.x1);
  gPropHost->propGetString(*image, kOfxImageEffectPropPixelDepth, 0, &depth);
  gPropHost->propGetString(*image, kOfxImageEffectPropComponents, 0, &components);
  gPropHost->propGetInt(*image, kOfxImagePropRowBytes, 0, &rowBytes);
  gPropHost->propGetPointer(*image, kOfxImagePropData, 0, &data);

  const int bitDepth = mapPixelDepth(depth);
  view.data = data;
  view.x1 = bounds.x1;
  view.y1 = bounds.y1;
  view.width = bounds.x2 - bounds.x1;
  view.height = bounds.y2 - bounds.y1;
  view.rowBytes = rowBytes;
  view.components = componentsForString(components);
  view.bytesPerComponent = bitDepth / 8;
  return data && view.components == 4 && view.bytesPerComponent > 0 ? kOfxStatOK : kOfxStatErrFormat;
}

OfxStatus fetchMutableImageView(
  OfxImageClipHandle clip,
  OfxTime time,
  OfxPropertySetHandle *image,
  spektrafilm::MutableImageView &view
) {
  spektrafilm::ImageView immutable{};
  OfxStatus status = fetchImageView(clip, time, image, immutable);
  if (status != kOfxStatOK) {
    return status;
  }
  view.data = const_cast<void *>(immutable.data);
  view.x1 = immutable.x1;
  view.y1 = immutable.y1;
  view.width = immutable.width;
  view.height = immutable.height;
  view.rowBytes = immutable.rowBytes;
  view.components = immutable.components;
  view.bytesPerComponent = immutable.bytesPerComponent;
  return kOfxStatOK;
}

void releaseImage(OfxPropertySetHandle image) {
  if (image) {
    gEffectHost->clipReleaseImage(image);
  }
}

#if SPEKTRAFILM_OFX_METAL_GPU_BUFFERS
struct MetalGpuImageProbe {
  OfxRectI bounds{};
  char *depth = nullptr;
  char *components = nullptr;
  void *buffer = nullptr;
  int rowBytes = 0;
};

spektrafilm::MetalBufferImageView makeMetalBufferImageView(const MetalGpuImageProbe &probe) {
  spektrafilm::MetalBufferImageView view{};
  view.buffer = probe.buffer;
  view.x1 = probe.bounds.x1;
  view.y1 = probe.bounds.y1;
  view.width = probe.bounds.x2 - probe.bounds.x1;
  view.height = probe.bounds.y2 - probe.bounds.y1;
  view.rowBytes = probe.rowBytes;
  view.components = componentsForString(probe.components);
  view.bytesPerComponent = mapPixelDepth(probe.depth) / 8;
  return view;
}

bool metalGpuBuffersEnabled(OfxPropertySetHandle inArgs) {
  int enabled = 0;
  return inArgs &&
         gPropHost->propGetInt(inArgs, kOfxImageEffectPropMetalEnabled, 0, &enabled) == kOfxStatOK &&
         enabled != 0;
}

void *metalCommandQueueFromArgs(OfxPropertySetHandle inArgs) {
  void *queue = nullptr;
  if (inArgs) {
    gPropHost->propGetPointer(inArgs, kOfxImageEffectPropMetalCommandQueue, 0, &queue);
  }
  return queue;
}

OfxStatus fetchMetalGpuImageProbe(
  OfxImageClipHandle clip,
  OfxTime time,
  OfxPropertySetHandle *image,
  MetalGpuImageProbe &probe
) {
  if (gEffectHost->clipGetImage(clip, time, nullptr, image) != kOfxStatOK || !*image) {
    return kOfxStatFailed;
  }

  gPropHost->propGetIntN(*image, kOfxImagePropBounds, 4, &probe.bounds.x1);
  gPropHost->propGetString(*image, kOfxImageEffectPropPixelDepth, 0, &probe.depth);
  gPropHost->propGetString(*image, kOfxImageEffectPropComponents, 0, &probe.components);
  gPropHost->propGetInt(*image, kOfxImagePropRowBytes, 0, &probe.rowBytes);
  gPropHost->propGetPointer(*image, kOfxImagePropData, 0, &probe.buffer);
  return probe.buffer ? kOfxStatOK : kOfxStatErrFormat;
}

OfxStatus probeMetalGpuBufferRender(
  OfxImageEffectHandle effect,
  InstanceData *data,
  OfxTime time,
  OfxPropertySetHandle inArgs,
  const spektrafilm::RenderWindow &window,
  const spektrafilm::RenderParams &params
) {
  OfxPropertySetHandle sourceImage = nullptr;
  OfxPropertySetHandle outputImage = nullptr;
  MetalGpuImageProbe source{};
  MetalGpuImageProbe output{};
  OfxStatus status = kOfxStatOK;

  try {
    status = fetchMetalGpuImageProbe(data->sourceClip, time, &sourceImage, source);
    if (status != kOfxStatOK) {
      throw status;
    }
    status = fetchMetalGpuImageProbe(data->outputClip, time, &outputImage, output);
    if (status != kOfxStatOK) {
      throw status;
    }

    if (!data->metalGpuBufferProbeLogged && gMessageHost) {
      data->metalGpuBufferProbeLogged = true;
      gMessageHost->message(
        effect,
        kOfxMessageLog,
        "lookfilmlabMetalGpuBufferProbe",
        "OFX Metal GPU buffers enabled by host. queue=%p sourceBuffer=%p sourceBounds=[%d,%d,%d,%d] sourceRowBytes=%d sourceDepth=%s sourceComponents=%s outputBuffer=%p outputBounds=[%d,%d,%d,%d] outputRowBytes=%d outputDepth=%s outputComponents=%s",
        metalCommandQueueFromArgs(inArgs),
        source.buffer,
        source.bounds.x1,
        source.bounds.y1,
        source.bounds.x2,
        source.bounds.y2,
        source.rowBytes,
        source.depth ? source.depth : "",
        source.components ? source.components : "",
        output.buffer,
        output.bounds.x1,
        output.bounds.y1,
        output.bounds.x2,
        output.bounds.y2,
        output.rowBytes,
        output.depth ? output.depth : "",
        output.components ? output.components : ""
      );
    }

    auto *metalRenderer = dynamic_cast<spektrafilm::MetalRenderer *>(data->renderer.get());
    if (!metalRenderer) {
      status = static_cast<OfxStatus>(kOfxStatGPURenderFailed);
    } else if (!metalRenderer->renderMetalBuffers(
                 makeMetalBufferImageView(source),
                 makeMetalBufferImageView(output),
                 window,
                 params,
                 time,
                 metalCommandQueueFromArgs(inArgs))) {
      if (gMessageHost) {
        gMessageHost->message(
          effect,
          kOfxMessageLog,
          "lookfilmlabMetalGpuBufferFallback",
          "OFX Metal GPU buffer render fell back to CPU: %s",
          metalRenderer->lastError().c_str()
        );
      }
      status = static_cast<OfxStatus>(kOfxStatGPURenderFailed);
    }
  } catch (OfxStatus caught) {
    status = caught;
  } catch (...) {
    status = kOfxStatErrUnknown;
  }

  releaseImage(sourceImage);
  releaseImage(outputImage);
  if (status != kOfxStatOK) {
    return status;
  }

  return kOfxStatOK;
}
#endif

double getDoubleAtTime(OfxParamHandle handle, OfxTime time, double fallback = 0.0) {
  if (!handle) {
    return fallback;
  }
  double value = fallback;
  gParamHost->paramGetValueAtTime(handle, time, &value);
  return value;
}

void getDouble3DAtTime(OfxParamHandle handle, OfxTime time, double (&value)[3], const double (&fallback)[3]) {
  value[0] = fallback[0];
  value[1] = fallback[1];
  value[2] = fallback[2];
  if (!handle) {
    return;
  }
  gParamHost->paramGetValueAtTime(handle, time, &value[0], &value[1], &value[2]);
}

int getIntAtTime(OfxParamHandle handle, OfxTime time, int fallback = 0) {
  if (!handle) {
    return fallback;
  }
  int value = fallback;
  gParamHost->paramGetValueAtTime(handle, time, &value);
  return value;
}

bool getBoolAtTime(OfxParamHandle handle, OfxTime time, bool fallback = false) {
  return getIntAtTime(handle, time, fallback ? 1 : 0) != 0;
}

bool getBoolValue(OfxParamHandle handle, bool fallback = false) {
  if (!handle) {
    return fallback;
  }
  int value = fallback ? 1 : 0;
  gParamHost->paramGetValue(handle, &value);
  return value != 0;
}

int getIntValue(OfxParamHandle handle, int fallback = 0) {
  if (!handle) {
    return fallback;
  }
  int value = fallback;
  gParamHost->paramGetValue(handle, &value);
  return value;
}

void setParamSecret(OfxParamHandle handle, bool secret) {
  if (!handle || !gParamHost || !gPropHost) {
    return;
  }
  OfxPropertySetHandle props = nullptr;
  if (gParamHost->paramGetPropertySet(handle, &props) != kOfxStatOK || !props) {
    return;
  }
  gPropHost->propSetInt(props, kOfxParamPropSecret, 0, secret ? 1 : 0);
  gPropHost->propSetInt(props, kOfxParamPropEnabled, 0, secret ? 0 : 1);
}

void setParamEnabled(OfxParamHandle handle, bool enabled) {
  if (!handle || !gParamHost || !gPropHost) {
    return;
  }
  OfxPropertySetHandle props = nullptr;
  if (gParamHost->paramGetPropertySet(handle, &props) != kOfxStatOK || !props) {
    return;
  }
  gPropHost->propSetInt(props, kOfxParamPropEnabled, 0, enabled ? 1 : 0);
}

void setParamSecretForFlavor(OfxParamHandle handle, const char *name, bool secret) {
  setParamSecret(handle, secret || parameterHiddenInFlavor(name));
}

void setGroupSecretForFlavor(OfxParamHandle handle, const char *name, bool secret) {
  setParamSecret(handle, secret || !groupVisibleInFlavor(name));
}

void syncConditionalParamVisibility(InstanceData *data) {
  if (!data) {
    return;
  }

  const spektrafilm::OutputRole outputRole = outputRoleForFlavor(
    getIntValue(data->outputRole, static_cast<int>(spektrafilm::OutputRole::DisplaySdr))
  );
  const bool hdrOutput = outputRole == spektrafilm::OutputRole::DisplayHdr;
  const bool rcmOutput = outputRole == spektrafilm::OutputRole::Rcm;
  const int processValue = getIntValue(data->process, 0);
  const bool scanNegative = processValue == static_cast<int>(spektrafilm::ProcessMode::ScanNegative);
  const bool printSimulation = processValue == static_cast<int>(spektrafilm::ProcessMode::PrintSimulation);
  const bool processNegative = processValue == static_cast<int>(spektrafilm::ProcessMode::ProcessNegative);
  const bool scannedNegativeBypass =
    isProductScan() ||
    (printSimulation &&
     getIntValue(data->printSource, 0) == static_cast<int>(spektrafilm::PrintSourceMode::ScannedNegativeBypass));
  setParamSecretForFlavor(data->scanNegativeInvert, "scanNegativeInvert", !scanNegative);
  setParamSecretForFlavor(data->inputColorSpace, "inputColorSpace", true);
  setParamSecretForFlavor(data->inputPrimariesColorSpace, "inputPrimariesColorSpace", false);
  setParamSecretForFlavor(data->inputTransferColorSpace, "inputTransferColorSpace", false);
  setParamSecretForFlavor(data->rcmInputColorSpace, "rcmInputColorSpace", true);
  setParamSecretForFlavor(data->sdrOutputColorSpace, "sdrOutputColorSpace", true);
  setParamSecretForFlavor(data->outputPrimariesColorSpace, "outputPrimariesColorSpace", hdrOutput);
  setParamSecretForFlavor(data->outputTransferColorSpace, "outputTransferColorSpace", hdrOutput);
  setParamSecretForFlavor(data->sceneOutputColorSpace, "sceneOutputColorSpace", true);
  setParamSecretForFlavor(data->hdrPreset, "hdrPreset", !hdrOutput);
  setParamSecretForFlavor(data->hdrTransfer, "hdrTransfer", !hdrOutput);
  setParamSecretForFlavor(data->hdrReferenceWhiteNits, "hdrReferenceWhiteNits", !hdrOutput);
  setParamSecretForFlavor(data->hdrPeakNits, "hdrPeakNits", !hdrOutput);
  setParamSecretForFlavor(data->hdrExposureEv, "hdrExposureEv", !hdrOutput);
  setParamSecretForFlavor(data->hdrToneMapping, "hdrToneMapping", !hdrOutput);

  const bool colorAdaptationEnabled = getBoolValue(data->colorAdaptation, false);
  setParamSecretForFlavor(data->colorAdaptationInputCompression, "colorAdaptationInputCompression", !colorAdaptationEnabled);
  setParamSecretForFlavor(data->colorAdaptationCurveSmoothing, "colorAdaptationCurveSmoothing", !colorAdaptationEnabled);
  setParamSecretForFlavor(data->colorAdaptationOutputLightnessCompression, "colorAdaptationOutputLightnessCompression", !colorAdaptationEnabled || rcmOutput);
  setParamSecretForFlavor(data->colorAdaptationOutputChromaCompression, "colorAdaptationOutputChromaCompression", !colorAdaptationEnabled || rcmOutput);

  setGroupSecretForFlavor(data->filteringGroup, "filteringGroup", processNegative);
  setParamSecretForFlavor(data->cameraUvFilterEnabled, "cameraUvFilterEnabled", processNegative);
  setParamSecretForFlavor(data->cameraUvCutNm, "cameraUvCutNm", processNegative);
  setParamSecretForFlavor(data->cameraIrFilterEnabled, "cameraIrFilterEnabled", processNegative);
  setParamSecretForFlavor(data->cameraIrCutNm, "cameraIrCutNm", processNegative);

  const bool printStageHidden = scanNegative;
  setGroupSecretForFlavor(data->enlargerGroup, "enlargerGroup", processNegative || printStageHidden);
  setParamSecretForFlavor(data->enlargerScale, "enlargerScale", processNegative || printStageHidden);
  setParamSecretForFlavor(data->enlargerOffsetXPercent, "enlargerOffsetXPercent", processNegative || printStageHidden);
  setParamSecretForFlavor(data->enlargerOffsetYPercent, "enlargerOffsetYPercent", processNegative || printStageHidden);

  setGroupSecretForFlavor(data->filmGroup, "filmGroup", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->rgbToRawMethod, "rgbToRawMethod", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->film, "film", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->filmFormat, "filmFormat", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->filmExposureEv, "filmExposureEv", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->autoExposure, "autoExposure", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->autoExposureMethod, "autoExposureMethod", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->filmPushPullMode, "filmPushPullMode", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->filmPushPullStops, "filmPushPullStops", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->negativeBleachBypassAmount, "negativeBleachBypassAmount", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->negativeLeucoCyanCoupling, "negativeLeucoCyanCoupling", processNegative || scannedNegativeBypass);
  setParamSecretForFlavor(data->filmGamma, "filmGamma", processNegative || scannedNegativeBypass);

  setGroupSecretForFlavor(data->printSourceGroup, "printSourceGroup", printStageHidden || isProProductionBuild());
  setGroupSecretForFlavor(data->printGroup, "printGroup", printStageHidden);
  setGroupSecretForFlavor(data->filmScanGroup, "filmScanGroup", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setGroupSecretForFlavor(data->scannedNegativeGroup, "scannedNegativeGroup", printStageHidden || !scannedNegativeBypass || !isProductScan());
  setParamSecretForFlavor(data->paper, "paper", printStageHidden);
  setParamSecretForFlavor(data->printTiming, "printTiming", printStageHidden);
  setParamSecretForFlavor(data->printSource, "printSource", printStageHidden || isProProductionBuild());
  setParamSecretForFlavor(data->scanInputEncoding, "scanInputEncoding", true);
  setParamSecretForFlavor(data->scanInputColorSpace, "scanInputColorSpace", true);
  setParamSecretForFlavor(data->scanWorkingColorSpace, "scanWorkingColorSpace", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setParamSecretForFlavor(data->scanDensityBasis, "scanDensityBasis", true);
  setParamSecretForFlavor(data->scanFilmBaseRgb, "scanFilmBaseRgb", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setParamSecretForFlavor(data->scanFilmBaseColorRgb, "scanFilmBaseColorRgb", printStageHidden || !scannedNegativeBypass || (isProProductionBuild() && !isProductScan()));
  setParamSecretForFlavor(data->scanFilmBaseTemp, "scanFilmBaseTemp", printStageHidden || !scannedNegativeBypass || (isProProductionBuild() && !isProductScan()));
  setParamSecretForFlavor(data->scanFilmBaseTint, "scanFilmBaseTint", printStageHidden || !scannedNegativeBypass || (isProProductionBuild() && !isProductScan()));
  setParamSecretForFlavor(data->scanBlackFlareRgb, "scanBlackFlareRgb", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setParamSecretForFlavor(data->scanExposureEv, "scanExposureEv", printStageHidden || !scannedNegativeBypass || (isProProductionBuild() && !isProductScan()));
  setParamSecretForFlavor(data->scanDensityContrast, "scanDensityContrast", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setParamSecretForFlavor(data->scanDensityScaleRgb, "scanDensityScaleRgb", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setParamSecretForFlavor(data->scanDensityScaleR, "scanDensityScaleR", !isProductScan());
  setParamSecretForFlavor(data->scanDensityScaleG, "scanDensityScaleG", !isProductScan());
  setParamSecretForFlavor(data->scanDensityScaleB, "scanDensityScaleB", !isProductScan());
  setParamSecretForFlavor(data->scanDensityOffsetRgb, "scanDensityOffsetRgb", printStageHidden || !scannedNegativeBypass || isProProductionBuild());
  setParamSecretForFlavor(data->printPushPullStops, "printPushPullStops", printStageHidden);
  setParamSecretForFlavor(data->printBleachBypassAmount, "printBleachBypassAmount", printStageHidden);
  setParamSecretForFlavor(data->printExposureEv, "printExposureEv", printStageHidden);
  setParamSecretForFlavor(data->printGamma, "printGamma", printStageHidden);
  setParamSecretForFlavor(data->printShadowShape, "printShadowShape", printStageHidden);
  setParamSecretForFlavor(data->printHighlightShape, "printHighlightShape", printStageHidden);

  setGroupSecretForFlavor(data->couplerGroup, "couplerGroup", processNegative);
  setParamSecretForFlavor(data->dirAmount, "dirCouplersAmount", processNegative);
  setParamSecretForFlavor(data->dirDiffusionUm, "dirCouplersDiffusionUm", processNegative);
  setParamSecretForFlavor(data->dirDiffusionTailUm, "dirCouplersDiffusionTailUm", processNegative);
  setParamSecretForFlavor(data->dirDiffusionTailWeight, "dirCouplersDiffusionTailWeight", processNegative);
  setParamSecretForFlavor(data->dirInhibitionSameLayer, "dirCouplersInhibitionSameLayer", processNegative);
  setParamSecretForFlavor(data->dirInhibitionInterlayer, "dirCouplersInhibitionInterlayer", processNegative);
  setParamSecretForFlavor(data->dirGammaSameLayerRgb, "dirGammaSameLayerRgb", processNegative);
  setParamSecretForFlavor(data->dirGammaRToGb, "dirGammaRToGb", processNegative);
  setParamSecretForFlavor(data->dirGammaGToRb, "dirGammaGToRb", processNegative);
  setParamSecretForFlavor(data->dirGammaBToRg, "dirGammaBToRg", processNegative);
  setParamSecretForFlavor(data->dirCalibrateToStock, "dirCalibrateToStock", processNegative);
  setParamSecretForFlavor(data->dirUsesStockCalibration, "dirUsesStockCalibration", true);

  setGroupSecretForFlavor(data->grainGroup, "grainGroup", processNegative);
  setParamSecretForFlavor(data->grainEnabled, "grainEnabled", processNegative);
  setParamSecretForFlavor(data->grainModel, "grainModel", processNegative);
  setParamSecretForFlavor(data->grainAmount, "grainAmount", processNegative);
  setParamSecretForFlavor(data->grainSaturation, "grainSaturation", processNegative);
  setParamSecretForFlavor(data->grainSublayersEnabled, "grainSublayersEnabled", processNegative);
  setParamSecretForFlavor(data->grainSubLayerCount, "grainSubLayerCount", processNegative);
  setParamSecretForFlavor(data->grainParticleAreaUm2, "grainParticleAreaUm2", processNegative);
  setParamSecretForFlavor(data->grainParticleScale, "grainParticleScale", processNegative);
  setParamSecretForFlavor(data->grainParticleScaleLayers, "grainParticleScaleLayers", processNegative);
  setParamSecretForFlavor(data->grainDensityMin, "grainDensityMin", processNegative);
  setParamSecretForFlavor(data->grainUniformity, "grainUniformity", processNegative);
  setParamSecretForFlavor(data->grainFinalBlurUm, "grainFinalBlurUm", processNegative);
  setParamSecretForFlavor(data->grainBlurDyeCloudsUm, "grainBlurDyeCloudsUm", processNegative);
  setParamSecretForFlavor(data->grainMicroStructure, "grainMicroStructure", processNegative);
  setParamSecretForFlavor(data->grainSeed, "grainSeed", processNegative);
  setParamSecretForFlavor(data->grainAnimate, "grainAnimate", processNegative);

  setGroupSecretForFlavor(data->grainSynthesisGroup, "grainSynthesisGroup", processNegative || !flavorAllowsDevelopmentControls());
  setParamSecretForFlavor(data->grainSynthesisSamples, "grainSynthesisSamples", processNegative);
  setParamSecretForFlavor(data->grainSynthesisMeanRadiusUm, "grainSynthesisMeanRadiusUm", processNegative);
  setParamSecretForFlavor(data->grainSynthesisRadiusStdDevRatio, "grainSynthesisRadiusStdDevRatio", processNegative);
  setParamSecretForFlavor(data->grainSynthesisObservationSigmaUm, "grainSynthesisObservationSigmaUm", processNegative);
  setParamSecretForFlavor(data->grainSynthesisCellSizeRatio, "grainSynthesisCellSizeRatio", processNegative);
  setParamSecretForFlavor(data->grainSynthesisMaxRadiusQuantile, "grainSynthesisMaxRadiusQuantile", processNegative);
  setParamSecretForFlavor(data->grainSynthesisCoverageEpsilon, "grainSynthesisCoverageEpsilon", processNegative);
  setParamSecretForFlavor(data->grainSynthesisMaxGrainsPerCell, "grainSynthesisMaxGrainsPerCell", processNegative);
  setParamSecretForFlavor(data->grainSynthesisRadiusScale, "grainSynthesisRadiusScale", processNegative);
  setParamSecretForFlavor(data->grainSynthesisLayerScale, "grainSynthesisLayerScale", processNegative);
  setParamSecretForFlavor(data->grainSynthesisLayered, "grainSynthesisLayered", processNegative);

  setGroupSecretForFlavor(data->halationGroup, "halationGroup", processNegative);
  setParamSecretForFlavor(data->halationEnabled, "halationEnabled", processNegative);
  setParamSecretForFlavor(data->scatterAmount, "scatterAmount", processNegative);
  setParamSecretForFlavor(data->scatterScale, "scatterScale", processNegative);
  setParamSecretForFlavor(data->halationAmount, "halationAmount", processNegative);
  setParamSecretForFlavor(data->halationScale, "halationScale", processNegative);
  setParamSecretForFlavor(data->halationStrength, "halationStrength", processNegative);
  setParamSecretForFlavor(data->halationBoostEv, "halationBoostEv", processNegative);
  setParamSecretForFlavor(data->halationBoostRange, "halationBoostRange", processNegative);
  setParamSecretForFlavor(data->halationProtectEv, "halationProtectEv", processNegative);

  setParamSecretForFlavor(data->cameraDiffusionEnabled, "cameraDiffusionEnabled", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionFamily, "cameraDiffusionFamily", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionStrength, "cameraDiffusionStrength", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionSpatialScale, "cameraDiffusionSpatialScale", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionHaloWarmth, "cameraDiffusionHaloWarmth", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionCoreIntensity, "cameraDiffusionCoreIntensity", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionCoreSize, "cameraDiffusionCoreSize", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionHaloIntensity, "cameraDiffusionHaloIntensity", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionHaloSize, "cameraDiffusionHaloSize", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionBloomIntensity, "cameraDiffusionBloomIntensity", processNegative);
  setParamSecretForFlavor(data->cameraDiffusionBloomSize, "cameraDiffusionBloomSize", processNegative);

  setParamSecretForFlavor(data->printDiffusionEnabled, "printDiffusionEnabled", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionFamily, "printDiffusionFamily", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionStrength, "printDiffusionStrength", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionSpatialScale, "printDiffusionSpatialScale", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionHaloWarmth, "printDiffusionHaloWarmth", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionCoreIntensity, "printDiffusionCoreIntensity", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionCoreSize, "printDiffusionCoreSize", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionHaloIntensity, "printDiffusionHaloIntensity", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionHaloSize, "printDiffusionHaloSize", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionBloomIntensity, "printDiffusionBloomIntensity", printStageHidden);
  setParamSecretForFlavor(data->printDiffusionBloomSize, "printDiffusionBloomSize", printStageHidden);

  const bool scannerHidden = rcmOutput;
  const bool printScanGlareHidden = scannerHidden || scanNegative;
  setGroupSecretForFlavor(data->scannerGroup, "scannerGroup", scannerHidden);
  setParamSecretForFlavor(data->scannerEnabled, "scannerEnabled", scannerHidden);
  setParamSecretForFlavor(data->scannerWhiteCorrection, "scannerWhiteCorrection", scannerHidden);
  setParamSecretForFlavor(data->scannerBlackCorrection, "scannerBlackCorrection", scannerHidden);
  setParamSecretForFlavor(data->scannerWhiteLevel, "scannerWhiteLevel", scannerHidden);
  setParamSecretForFlavor(data->scannerBlackLevel, "scannerBlackLevel", scannerHidden);
  setParamSecretForFlavor(data->glarePercent, "glarePercent", printScanGlareHidden);
  setParamSecretForFlavor(data->glareRoughness, "glareRoughness", printScanGlareHidden);
  setParamSecretForFlavor(data->glareBlur, "glareBlur", printScanGlareHidden);
  setParamSecretForFlavor(data->scannerMtf50LpMm, "scannerMtf50LpMm", scannerHidden);
  setParamSecretForFlavor(data->scannerUnsharpRadiusUm, "scannerUnsharpRadiusUm", scannerHidden);
  setParamSecretForFlavor(data->scannerUnsharpAmount, "scannerUnsharpAmount", scannerHidden);

  const bool synthesisModel = !processNegative && getIntValue(data->grainModel, static_cast<int>(spektrafilm::GrainModel::Preview)) ==
    static_cast<int>(spektrafilm::GrainModel::GrainSynthesis);
  setParamSecretForFlavor(data->grainSynthesisSize, "grainSynthesisSize", !synthesisModel);
  setParamSecretForFlavor(data->grainSynthesisAmount, "grainSynthesisAmount", !synthesisModel);
  setParamSecretForFlavor(data->grainSynthesisSharpness, "grainSynthesisSharpness", !synthesisModel);
  setParamSecretForFlavor(data->grainSynthesisQuality, "grainSynthesisQuality", !synthesisModel);

  const bool sublayersEnabled = getBoolValue(data->grainSublayersEnabled, true);
  setParamSecretForFlavor(data->grainSubLayerCount, "grainSubLayerCount", processNegative || sublayersEnabled);

  const bool apdPrintTiming = spektrafilm::kSpektraAcademyPrinterDensityEnabled &&
    getIntValue(data->printTiming, static_cast<int>(spektrafilm::PrintTimingMode::FilteredEnlarger)) ==
    static_cast<int>(spektrafilm::PrintTimingMode::ApdPrinterDensity);
  setParamSecretForFlavor(data->filterC, "filterC", scanNegative || apdPrintTiming);
  setParamSecretForFlavor(data->filterMShift, "filterMShift", scanNegative || apdPrintTiming);
  setParamSecretForFlavor(data->filterYShift, "filterYShift", scanNegative || apdPrintTiming);
  setParamSecretForFlavor(data->preflashExposure, "preflashExposure", scanNegative || processNegative);
  setParamSecretForFlavor(data->preflashMFilterShift, "preflashMFilterShift", scanNegative || processNegative || apdPrintTiming);
  setParamSecretForFlavor(data->preflashYFilterShift, "preflashYFilterShift", scanNegative || processNegative || apdPrintTiming);
  setParamSecretForFlavor(data->printerLightsGang, "printerLightsGang", scanNegative || !apdPrintTiming);
  setParamSecretForFlavor(data->printerLightsGroup, "printerLightsGroup", scanNegative || !apdPrintTiming);
  setParamSecretForFlavor(data->printerLightR, "printerLightR", scanNegative || !apdPrintTiming);
  setParamSecretForFlavor(data->printerLightG, "printerLightG", scanNegative || !apdPrintTiming);
  setParamSecretForFlavor(data->printerLightB, "printerLightB", scanNegative || !apdPrintTiming);
  setParamSecretForFlavor(data->printerLightCalibration, "printerLightCalibration", scanNegative || !apdPrintTiming);

  const bool productionPrinterLightsEnabled = getBoolValue(data->productionPrinterLightsEnabled, true);
  setParamEnabled(data->productionPrinterLightsLinked, productionPrinterLightsEnabled);
  setParamEnabled(data->creativePrinterLightR, productionPrinterLightsEnabled);
  setParamEnabled(data->creativePrinterLightG, productionPrinterLightsEnabled);
  setParamEnabled(data->creativePrinterLightB, productionPrinterLightsEnabled);

  const bool lutExportAllowed = outputRole == spektrafilm::OutputRole::DisplaySdr;
  setParamEnabled(data->exportLut, lutExportAllowed);
}

bool readCurrentPrinterLights(InstanceData *data, double (&current)[3]) {
  if (!data || !data->printerLightR || !data->printerLightG || !data->printerLightB) {
    return false;
  }
  gParamHost->paramGetValue(data->printerLightR, &current[0]);
  gParamHost->paramGetValue(data->printerLightG, &current[1]);
  gParamHost->paramGetValue(data->printerLightB, &current[2]);
  return true;
}

void rememberCurrentPrinterLights(InstanceData *data, const double (&current)[3]) {
  if (!data) {
    return;
  }
  data->lastPrinterLights[0] = current[0];
  data->lastPrinterLights[1] = current[1];
  data->lastPrinterLights[2] = current[2];
  data->lastPrinterLightsInitialized = true;
}

void rememberCurrentPrinterLights(InstanceData *data) {
  double current[3] = {0.0, 0.0, 0.0};
  if (readCurrentPrinterLights(data, current)) {
    rememberCurrentPrinterLights(data, current);
  }
}

bool readCurrentCreativePrinterLights(InstanceData *data, double (&current)[3]) {
  if (!data || !data->creativePrinterLightR || !data->creativePrinterLightG || !data->creativePrinterLightB) {
    return false;
  }
  gParamHost->paramGetValue(data->creativePrinterLightR, &current[0]);
  gParamHost->paramGetValue(data->creativePrinterLightG, &current[1]);
  gParamHost->paramGetValue(data->creativePrinterLightB, &current[2]);
  return true;
}

void rememberCurrentCreativePrinterLights(InstanceData *data, const double (&current)[3]) {
  if (!data) {
    return;
  }
  data->lastCreativePrinterLights[0] = current[0];
  data->lastCreativePrinterLights[1] = current[1];
  data->lastCreativePrinterLights[2] = current[2];
  data->lastCreativePrinterLightsInitialized = true;
}

void rememberCurrentCreativePrinterLights(InstanceData *data) {
  double current[3] = {0.0, 0.0, 0.0};
  if (readCurrentCreativePrinterLights(data, current)) {
    rememberCurrentCreativePrinterLights(data, current);
  }
}

OfxStatus syncProductionCreativePrinterLights(InstanceData *data, const char *changedName) {
  if (!data || !changedName ||
      !data->productionPrinterLightsEnabled || !data->productionPrinterLightsLinked ||
      !data->creativePrinterLightR || !data->creativePrinterLightG || !data->creativePrinterLightB) {
    return kOfxStatReplyDefault;
  }
  if (data->syncingCreativePrinterLights) {
    return kOfxStatOK;
  }

  const bool enabledChanged = std::strcmp(changedName, "productionPrinterLightsEnabled") == 0;
  const bool linkedChanged = std::strcmp(changedName, "productionPrinterLightsLinked") == 0;
  const bool redChanged = std::strcmp(changedName, "creativePrinterLightR") == 0;
  const bool greenChanged = std::strcmp(changedName, "creativePrinterLightG") == 0;
  const bool blueChanged = std::strcmp(changedName, "creativePrinterLightB") == 0;
  const bool lightChanged = redChanged || greenChanged || blueChanged;
  if (!enabledChanged && !linkedChanged && !lightChanged) {
    return kOfxStatReplyDefault;
  }

  double current[3] = {0.0, 0.0, 0.0};
  if (!readCurrentCreativePrinterLights(data, current)) {
    return kOfxStatReplyDefault;
  }

  if (enabledChanged || linkedChanged) {
    rememberCurrentCreativePrinterLights(data, current);
    return kOfxStatOK;
  }

  if (!getBoolValue(data->productionPrinterLightsLinked, false) ||
      !data->lastCreativePrinterLightsInitialized) {
    rememberCurrentCreativePrinterLights(data, current);
    return kOfxStatReplyDefault;
  }

  int changedIndex = 0;
  if (greenChanged) {
    changedIndex = 1;
  } else if (blueChanged) {
    changedIndex = 2;
  }
  const double delta = current[changedIndex] - data->lastCreativePrinterLights[changedIndex];
  if (std::abs(delta) <= 1.0e-9) {
    rememberCurrentCreativePrinterLights(data, current);
    return kOfxStatReplyDefault;
  }

  double linked[3] = {
    std::clamp(data->lastCreativePrinterLights[0] + delta, -24.0, 24.0),
    std::clamp(data->lastCreativePrinterLights[1] + delta, -24.0, 24.0),
    std::clamp(data->lastCreativePrinterLights[2] + delta, -24.0, 24.0),
  };
  linked[changedIndex] = current[changedIndex];
  rememberCurrentCreativePrinterLights(data, linked);

  data->syncingCreativePrinterLights = true;
  if (std::abs(current[0] - linked[0]) > 1.0e-9) {
    gParamHost->paramSetValue(data->creativePrinterLightR, linked[0]);
  }
  if (std::abs(current[1] - linked[1]) > 1.0e-9) {
    gParamHost->paramSetValue(data->creativePrinterLightG, linked[1]);
  }
  if (std::abs(current[2] - linked[2]) > 1.0e-9) {
    gParamHost->paramSetValue(data->creativePrinterLightB, linked[2]);
  }
  data->syncingCreativePrinterLights = false;
  return kOfxStatOK;
}

const spektrafilm::ProfileCurveSet *currentFilmCurves(InstanceData *data) {
  int filmIndex = static_cast<int>(spektrafilm::kSpektraDefaultFilmIndex);
  if (data && data->film) {
    gParamHost->paramGetValue(data->film, &filmIndex);
  }
  const spektrafilm::ProfileCurveSet *curves = spektrafilm::filmProfileCurves(filmIndex);
  return curves ? curves : spektrafilm::filmProfileCurves(static_cast<int32_t>(spektrafilm::kSpektraDefaultFilmIndex));
}

bool dirUsesStockCalibration(InstanceData *data) {
  return data && data->dirUsesStockCalibration &&
    getBoolValue(data->dirUsesStockCalibration, true);
}

bool applyDirStockCalibration(InstanceData *data, bool resetMultipliers) {
  if (!data || !data->dirGammaSameLayerRgb || !data->dirGammaRToGb || !data->dirGammaGToRb ||
      !data->dirGammaBToRg || !data->dirUsesStockCalibration) {
    return false;
  }
  const spektrafilm::ProfileCurveSet *curves = currentFilmCurves(data);
  if (!curves || !curves->dirGammaSameLayerRgb || !curves->dirGammaRToGb ||
      !curves->dirGammaGToRb || !curves->dirGammaBToRg) {
    return false;
  }

  data->syncingDirCalibration = true;
  if (resetMultipliers) {
    if (data->dirInhibitionSameLayer) {
      gParamHost->paramSetValue(data->dirInhibitionSameLayer, 1.0);
    }
    if (data->dirInhibitionInterlayer) {
      gParamHost->paramSetValue(data->dirInhibitionInterlayer, 1.0);
    }
  }
  gParamHost->paramSetValue(
    data->dirGammaSameLayerRgb,
    curves->dirGammaSameLayerRgb[0],
    curves->dirGammaSameLayerRgb[1],
    curves->dirGammaSameLayerRgb[2]
  );
  gParamHost->paramSetValue(data->dirGammaRToGb, curves->dirGammaRToGb[0], curves->dirGammaRToGb[1]);
  gParamHost->paramSetValue(data->dirGammaGToRb, curves->dirGammaGToRb[0], curves->dirGammaGToRb[1]);
  gParamHost->paramSetValue(data->dirGammaBToRg, curves->dirGammaBToRg[0], curves->dirGammaBToRg[1]);
  gParamHost->paramSetValue(data->dirUsesStockCalibration, 1);
  data->syncingDirCalibration = false;
  return true;
}

struct HdrPresetValues {
  int transfer = 0;
  double referenceWhiteNits = 203.0;
  double peakNits = 1000.0;
  int toneMapping = 1;
};

HdrPresetValues hdrPresetValues(int preset) {
  switch (preset) {
    case 1:
      return {0, 203.0, 4000.0, 1};
    case 2:
      return {1, 203.0, 1000.0, 1};
    default:
      return {0, 203.0, 1000.0, 1};
  }
}

spektrafilm::RgbToRawMethod rgbToRawMethodFromChoice(int choice) {
  switch (choice) {
    case 1:
      return spektrafilm::RgbToRawMethod::Hanatos2025;
    case 2:
      return spektrafilm::RgbToRawMethod::Mallett2019;
    case 0:
    default:
      return spektrafilm::RgbToRawMethod::Hanatos2026;
  }
}

constexpr const char *kCineNegativeProfileLabels[] = {
  "Kodak Vision3 5219 500T",
  "Kodak Vision3 5207 250D",
  "Kodak Verita 200D",
  "Kodak Vision3 5213 200T",
  "Kodak Vision3 5203 50D",
};

constexpr int kCineNegativeProfileIndices[] = {12, 9, 10, 11, 8};

constexpr const char *kPhotoNegativeProfileLabels[] = {
  "Kodak Ektar 100",
  "Kodak Portra 160",
  "Kodak Portra 400",
  "Kodak Portra 800",
  "Kodak Portra 800 Push 1",
  "Kodak Portra 800 Push 2",
  "Kodak Gold 200",
  "Kodak Ultramax 400",
  "Fujifilm Pro 400H",
  "Fujifilm C200",
  "Fujifilm X-Tra 400",
};

constexpr int kPhotoNegativeProfileIndices[] = {0, 1, 2, 3, 4, 5, 6, 7, 13, 14, 15};

constexpr const char *kCinePrintProfileLabels[] = {
  "Kodak Vision Color 2383",
  "Kodak Vision Premier 2393",
};

constexpr int kCinePrintProfileIndices[] = {6, 7};

constexpr const char *kPhotoPaperProfileLabels[] = {
  "Kodak Professional Endura Premier",
  "Kodak Professional Ultra Endura",
  "Kodak Ektacolor Edge",
  "Kodak Professional Supra Endura",
  "Kodak Professional Portra Endura",
  "Fujifilm Crystal Archive Type II",
};

constexpr int kPhotoPaperProfileIndices[] = {0, 1, 2, 3, 4, 5};

constexpr const char *kScanPaperProfileLabels[] = {
  "Kodak Professional Endura Premier",
  "Kodak Professional Ultra Endura",
  "Kodak Ektacolor Edge",
  "Kodak Professional Supra Endura",
  "Kodak Professional Portra Endura",
  "Fujifilm Crystal Archive Type II",
  "Kodak Vision Color 2383",
  "Kodak Vision Premier 2393",
};

constexpr int kScanPaperProfileIndices[] = {0, 1, 2, 3, 4, 5, 6, 7};

int indexFromChoice(int value, const int *indices, size_t count, int fallback) {
  if (!indices || count == 0u) {
    return fallback;
  }
  const int clamped = std::clamp(value, 0, static_cast<int>(count - 1u));
  return indices[clamped];
}

int productionNegativeIndexFromChoice(int value) {
  if (isProductPhoto()) {
    return indexFromChoice(value, kPhotoNegativeProfileIndices, std::size(kPhotoNegativeProfileIndices), 2);
  }
  return indexFromChoice(value, kCineNegativeProfileIndices, std::size(kCineNegativeProfileIndices), 12);
}

int productionPrintIndexFromChoice(int value) {
  if (isProductScan()) {
    return indexFromChoice(value, kScanPaperProfileIndices, std::size(kScanPaperProfileIndices), 4);
  }
  if (isProductPhoto()) {
    return indexFromChoice(value, kPhotoPaperProfileIndices, std::size(kPhotoPaperProfileIndices), 4);
  }
  return indexFromChoice(value, kCinePrintProfileIndices, std::size(kCinePrintProfileIndices), 6);
}

void applyCreativePrinterLightTrims(spektrafilm::RenderParams &params, float red, float green, float blue) {
  const float cTrim = 0.5f * (green + blue) - red;
  const float mTrim = 0.5f * (red + blue) - green;
  const float yTrim = 0.5f * (red + green) - blue;
  params.filterC += cTrim;
  params.filterMShift += mTrim;
  params.filterYShift += yTrim;
}

void forceProductionRenderParams(spektrafilm::RenderParams &params) {
  params.process = spektrafilm::ProcessMode::PrintSimulation;
  params.outputRole = spektrafilm::OutputRole::DisplaySdr;
  params.scanNegativeInvert = false;
  params.colorAdaptation = false;
  params.colorAdaptationInputCompression = false;
  params.colorAdaptationCurveSmoothing = false;
  params.colorAdaptationOutputLightnessCompression = false;
  params.colorAdaptationOutputChromaCompression = false;
  params.printTiming = spektrafilm::PrintTimingMode::FilteredEnlarger;
  params.autoExposure = false;
  if (!isProductPhoto() && !isProductScan()) {
    params.printPushPullStops = 0.0f;
  }
  params.enlargerScale = 1.0f;
  params.enlargerOffsetXPercent = 0.0f;
  params.enlargerOffsetYPercent = 0.0f;
  params.grainEnabled = false;
  params.halationEnabled = false;
  params.cameraDiffusionEnabled = false;
  params.printDiffusionEnabled = false;
  params.scannerEnabled = false;
}

std::filesystem::path bundledResourcePath(const char *filename) {
  if (!filename || !filename[0]) {
    return {};
  }
#if defined __APPLE__
  Dl_info imageInfo{};
  if (dladdr(&gPluginImageAnchor, &imageInfo) == 0 || !imageInfo.dli_fname) {
    return {};
  }
  const std::filesystem::path imagePath(imageInfo.dli_fname);
  const std::filesystem::path contentsPath = imagePath.parent_path().parent_path();
  return contentsPath.empty() ? std::filesystem::path{} : contentsPath / "Resources" / filename;
#elif defined _WIN32
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&gPluginImageAnchor),
        &module
      )) {
    return {};
  }
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size() - 1) {
      const std::filesystem::path imagePath(std::wstring(buffer.data(), length));
      const std::filesystem::path contentsPath = imagePath.parent_path().parent_path();
      return contentsPath.empty() ? std::filesystem::path{} : contentsPath / "Resources" / filename;
    }
    buffer.resize(buffer.size() * 2u);
  }
  return {};
#elif defined __linux__
  Dl_info imageInfo{};
  if (dladdr(&gPluginImageAnchor, &imageInfo) == 0 || !imageInfo.dli_fname) {
    return {};
  }
  const std::filesystem::path imagePath(imageInfo.dli_fname);
  const std::filesystem::path contentsPath = imagePath.parent_path().parent_path();
  return contentsPath.empty() ? std::filesystem::path{} : contentsPath / "Resources" / filename;
#else
  return {};
#endif
}

bool readWholeTextFile(const std::filesystem::path &path, std::string &text) {
  text.clear();
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  return input.good() || input.eof();
}

size_t matchingJsonBrace(const std::string &text, size_t openPos) {
  if (openPos >= text.size() || text[openPos] != '{') {
    return std::string::npos;
  }
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = openPos; i < text.size(); ++i) {
    const char ch = text[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        inString = false;
      }
      continue;
    }
    if (ch == '"') {
      inString = true;
    } else if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

bool extractJsonObjectByKey(const std::string &text, const char *key, std::string &objectText) {
  objectText.clear();
  const std::string quotedKey = std::string("\"") + key + "\"";
  size_t pos = text.find(quotedKey);
  if (pos == std::string::npos) {
    return false;
  }
  pos = text.find('{', pos + quotedKey.size());
  if (pos == std::string::npos) {
    return false;
  }
  const size_t end = matchingJsonBrace(text, pos);
  if (end == std::string::npos) {
    return false;
  }
  objectText = text.substr(pos, end - pos + 1u);
  return true;
}

bool parseJsonIntField(const std::string &text, const char *key, int &value) {
  const std::string quotedKey = std::string("\"") + key + "\"";
  size_t pos = text.find(quotedKey);
  if (pos == std::string::npos) {
    return false;
  }
  pos = text.find(':', pos + quotedKey.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  char *end = nullptr;
  const long parsed = std::strtol(text.c_str() + pos, &end, 10);
  if (end == text.c_str() + pos) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool parseStoredParamJsonObject(const std::string &objectText, StoredParamValue &value) {
  int kindRaw = 0;
  if (!parseJsonIntField(objectText, "kind", kindRaw)) {
    return false;
  }
  value = {};
  value.kind = static_cast<ParamValueKind>(kindRaw);
  if (value.kind != ParamValueKind::Int &&
      value.kind != ParamValueKind::Bool &&
      value.kind != ParamValueKind::Double &&
      value.kind != ParamValueKind::Double2D &&
      value.kind != ParamValueKind::Double3D) {
    return false;
  }
  const int components = paramComponentCount(value.kind);
  const std::string valueKey = "\"value\"";
  size_t pos = objectText.find(valueKey);
  if (pos == std::string::npos) {
    return false;
  }
  pos = objectText.find('[', pos + valueKey.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  for (int i = 0; i < components; ++i) {
    while (pos < objectText.size() && (std::isspace(static_cast<unsigned char>(objectText[pos])) || objectText[pos] == ',')) {
      ++pos;
    }
    char *end = nullptr;
    if (paramKindUsesDouble(value.kind)) {
      const double parsed = std::strtod(objectText.c_str() + pos, &end);
      if (end == objectText.c_str() + pos) {
        return false;
      }
      value.doubleValue[i] = parsed;
    } else {
      const long parsed = std::strtol(objectText.c_str() + pos, &end, 10);
      if (end == objectText.c_str() + pos) {
        return false;
      }
      value.intValue[i] = static_cast<int>(parsed);
    }
    pos = static_cast<size_t>(end - objectText.c_str());
  }
  return true;
}

bool parseCalibrationSection(const std::string &sectionText, DefaultsSnapshot &snapshot) {
  snapshot.clear();
  size_t pos = 0;
  while (true) {
    const size_t nameStart = sectionText.find('"', pos);
    if (nameStart == std::string::npos) {
      break;
    }
    const size_t nameEnd = sectionText.find('"', nameStart + 1u);
    if (nameEnd == std::string::npos) {
      return false;
    }
    const std::string name = sectionText.substr(nameStart + 1u, nameEnd - nameStart - 1u);
    const size_t objectStart = sectionText.find('{', nameEnd + 1u);
    if (objectStart == std::string::npos) {
      return false;
    }
    const size_t objectEnd = matchingJsonBrace(sectionText, objectStart);
    if (objectEnd == std::string::npos) {
      return false;
    }
    StoredParamValue stored{};
    const ParamDefault *factory = defaultForParam(name.c_str());
    if (factory && parseStoredParamJsonObject(sectionText.substr(objectStart, objectEnd - objectStart + 1u), stored) &&
        stored.kind == factory->kind && calibrationParam(name.c_str())) {
      snapshot[name] = stored;
    }
    pos = objectEnd + 1u;
  }
  return true;
}

bool decodeCalibrationSnapshotJson(const std::string &text, CalibrationSnapshot &snapshot) {
  if (text.find("\"format\"") == std::string::npos ||
      text.find("lookfilmlab-calibration-v1") == std::string::npos) {
    return false;
  }
  std::string sectionsText;
  if (!extractJsonObjectByKey(text, "sections", sectionsText)) {
    return false;
  }
  CalibrationSnapshot decoded{};
  std::string section;
  if (extractJsonObjectByKey(sectionsText, "global", section) && !parseCalibrationSection(section, decoded.global)) {
    return false;
  }
  if (extractJsonObjectByKey(sectionsText, "negative", section) && !parseCalibrationSection(section, decoded.negative)) {
    return false;
  }
  if (extractJsonObjectByKey(sectionsText, "print", section) && !parseCalibrationSection(section, decoded.print)) {
    return false;
  }
  if (extractJsonObjectByKey(sectionsText, "pairOverride", section) && !parseCalibrationSection(section, decoded.pairOverride)) {
    return false;
  }
  snapshot = std::move(decoded);
  return true;
}

bool readCalibrationSnapshotFile(const std::filesystem::path &path, CalibrationSnapshot &snapshot, std::string &error) {
  error.clear();
  std::string text;
  if (!std::filesystem::is_regular_file(path)) {
    error = "Calibration file not found: " + path.string();
    return false;
  }
  if (!readWholeTextFile(path, text)) {
    error = "Could not read calibration file: " + path.string();
    return false;
  }
  if (!decodeCalibrationSnapshotJson(text, snapshot)) {
    error = "Calibration file is not a recognized LookFilmLab calibration: " + path.string();
    return false;
  }
  return true;
}

struct BundledCalibrationCache {
  bool attempted = false;
  bool available = false;
  CalibrationSnapshot snapshot{};
};

const CalibrationSnapshot *bundledProductionCalibration() {
  static std::mutex mutex;
  static BundledCalibrationCache cache{};
  std::lock_guard<std::mutex> lock(mutex);
  if (!cache.attempted) {
    cache.attempted = true;
    const std::filesystem::path path = bundledResourcePath("production_calibration.lookfilmlab.json");
    std::string text;
    if (!path.empty() && std::filesystem::is_regular_file(path) && readWholeTextFile(path, text)) {
      cache.available = decodeCalibrationSnapshotJson(text, cache.snapshot);
    }
  }
  return cache.available ? &cache.snapshot : nullptr;
}

double storedDoubleValue(const StoredParamValue &value, int component, double fallback) {
  return paramKindUsesDouble(value.kind) && component >= 0 && component < paramComponentCount(value.kind)
    ? value.doubleValue[component]
    : fallback;
}

int storedIntValue(const StoredParamValue &value, int component, int fallback) {
  return !paramKindUsesDouble(value.kind) && component >= 0 && component < paramComponentCount(value.kind)
    ? value.intValue[component]
    : fallback;
}

void applyCalibrationSnapshotToRenderParams(spektrafilm::RenderParams &params, const DefaultsSnapshot &snapshot) {
  for (const auto &item : snapshot) {
    const std::string &name = item.first;
    const StoredParamValue &value = item.second;
    if (name == "cameraUvFilterEnabled") {
      params.cameraUvFilterEnabled = storedIntValue(value, 0, params.cameraUvFilterEnabled ? 1 : 0) != 0;
    } else if (name == "cameraUvCutNm") {
      params.cameraUvCutNm = static_cast<float>(storedDoubleValue(value, 0, params.cameraUvCutNm));
    } else if (name == "cameraIrFilterEnabled") {
      params.cameraIrFilterEnabled = storedIntValue(value, 0, params.cameraIrFilterEnabled ? 1 : 0) != 0;
    } else if (name == "cameraIrCutNm") {
      params.cameraIrCutNm = static_cast<float>(storedDoubleValue(value, 0, params.cameraIrCutNm));
    } else if (name == "rgbToRawMethod") {
      params.rgbToRawMethod = rgbToRawMethodFromChoice(storedIntValue(value, 0, 0));
    } else if (name == "filmGamma") {
      params.filmGamma = static_cast<float>(storedDoubleValue(value, 0, params.filmGamma));
    } else if (name == "negativeLeucoCyanCoupling") {
      params.negativeLeucoCyanCoupling = static_cast<float>(storedDoubleValue(value, 0, params.negativeLeucoCyanCoupling));
    } else if (name == "printExposureEv") {
      params.printExposureEv = static_cast<float>(storedDoubleValue(value, 0, params.printExposureEv));
    } else if (name == "printGamma") {
      params.printGamma = static_cast<float>(storedDoubleValue(value, 0, params.printGamma));
    } else if (name == "printShadowShape") {
      params.printShadowShape = static_cast<float>(storedDoubleValue(value, 0, params.printShadowShape));
    } else if (name == "printHighlightShape") {
      params.printHighlightShape = static_cast<float>(storedDoubleValue(value, 0, params.printHighlightShape));
    } else if (name == "filterC") {
      params.filterC = static_cast<float>(storedDoubleValue(value, 0, params.filterC));
    } else if (name == "filterMShift") {
      params.filterMShift = static_cast<float>(storedDoubleValue(value, 0, params.filterMShift));
    } else if (name == "filterYShift") {
      params.filterYShift = static_cast<float>(storedDoubleValue(value, 0, params.filterYShift));
    } else if (name == "preflashExposure") {
      params.preflashExposure = static_cast<float>(storedDoubleValue(value, 0, params.preflashExposure));
    } else if (name == "preflashMFilterShift") {
      params.preflashMFilterShift = static_cast<float>(storedDoubleValue(value, 0, params.preflashMFilterShift));
    } else if (name == "preflashYFilterShift") {
      params.preflashYFilterShift = static_cast<float>(storedDoubleValue(value, 0, params.preflashYFilterShift));
    } else if (name == "dirAmount") {
      params.dirCouplersAmount = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersAmount));
    } else if (name == "dirDiffusionUm") {
      params.dirCouplersDiffusionUm = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersDiffusionUm));
    } else if (name == "dirDiffusionTailUm") {
      params.dirCouplersDiffusionTailUm = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersDiffusionTailUm));
    } else if (name == "dirDiffusionTailWeight") {
      params.dirCouplersDiffusionTailWeight = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersDiffusionTailWeight));
    } else if (name == "dirInhibitionSameLayer") {
      params.dirCouplersInhibitionSameLayer = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersInhibitionSameLayer));
    } else if (name == "dirInhibitionInterlayer") {
      params.dirCouplersInhibitionInterlayer = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersInhibitionInterlayer));
    } else if (name == "dirGammaSameLayerRgb") {
      params.dirCouplersGammaSameLayerR = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersGammaSameLayerR));
      params.dirCouplersGammaSameLayerG = static_cast<float>(storedDoubleValue(value, 1, params.dirCouplersGammaSameLayerG));
      params.dirCouplersGammaSameLayerB = static_cast<float>(storedDoubleValue(value, 2, params.dirCouplersGammaSameLayerB));
    } else if (name == "dirGammaRToGb") {
      params.dirCouplersGammaRToG = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersGammaRToG));
      params.dirCouplersGammaRToB = static_cast<float>(storedDoubleValue(value, 1, params.dirCouplersGammaRToB));
    } else if (name == "dirGammaGToRb") {
      params.dirCouplersGammaGToR = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersGammaGToR));
      params.dirCouplersGammaGToB = static_cast<float>(storedDoubleValue(value, 1, params.dirCouplersGammaGToB));
    } else if (name == "dirGammaBToRg") {
      params.dirCouplersGammaBToR = static_cast<float>(storedDoubleValue(value, 0, params.dirCouplersGammaBToR));
      params.dirCouplersGammaBToG = static_cast<float>(storedDoubleValue(value, 1, params.dirCouplersGammaBToG));
    }
  }
}

void applyBundledProductionCalibration(spektrafilm::RenderParams &params) {
  const CalibrationSnapshot *snapshot = bundledProductionCalibration();
  if (!snapshot) {
    return;
  }
  applyCalibrationSnapshotToRenderParams(params, snapshot->global);
  applyCalibrationSnapshotToRenderParams(params, snapshot->negative);
  applyCalibrationSnapshotToRenderParams(params, snapshot->print);
  applyCalibrationSnapshotToRenderParams(params, snapshot->pairOverride);
}

spektrafilm::RenderParams readParams(InstanceData *data, OfxTime time) {
  constexpr spektrafilm::ColorSpace kScanInputColorSpaces[] = {
    spektrafilm::ColorSpace::LinearRec709,
    spektrafilm::ColorSpace::Srgb,
    spektrafilm::ColorSpace::AdobeRgb1998,
    spektrafilm::ColorSpace::DisplayP3,
    spektrafilm::ColorSpace::ProPhotoRgb,
    spektrafilm::ColorSpace::Rec709Gamma24,
    spektrafilm::ColorSpace::Rec709Gamma22,
    spektrafilm::ColorSpace::LinearP3D65,
    spektrafilm::ColorSpace::LinearRec2020,
    spektrafilm::ColorSpace::AcesCg,
  };
  constexpr spektrafilm::ColorSpace kScanWorkingColorSpaces[] = {
    spektrafilm::ColorSpace::LinearRec2020,
    spektrafilm::ColorSpace::AcesCg,
    spektrafilm::ColorSpace::LinearRec709,
  };
  constexpr spektrafilm::ColorSpace kPrimariesColorSpaces[] = {
    spektrafilm::ColorSpace::ArriLogC4,
    spektrafilm::ColorSpace::ArriLogC3Ei800,
    spektrafilm::ColorSpace::BmdFilmWideGamutGen5,
    spektrafilm::ColorSpace::DavinciIntermediateWideGamut,
    spektrafilm::ColorSpace::RedLog3G10RedWideGamutRgb,
    spektrafilm::ColorSpace::SonySLog3SGamut3,
    spektrafilm::ColorSpace::SonySLog3SGamut3Cine,
    spektrafilm::ColorSpace::CanonLog3CinemaGamutD55,
    spektrafilm::ColorSpace::PanasonicVLogVGamut,
    spektrafilm::ColorSpace::Aces2065_1,
    spektrafilm::ColorSpace::AcesCg,
    spektrafilm::ColorSpace::LinearRec2020,
    spektrafilm::ColorSpace::LinearRec709,
    spektrafilm::ColorSpace::LinearP3D65,
    spektrafilm::ColorSpace::DisplayP3,
    spektrafilm::ColorSpace::ProPhotoRgb,
    spektrafilm::ColorSpace::AdobeRgb1998,
    spektrafilm::ColorSpace::DciP3,
  };
  constexpr spektrafilm::ColorSpace kTransferColorSpaces[] = {
    spektrafilm::ColorSpace::ArriLogC4,
    spektrafilm::ColorSpace::ArriLogC3Ei800,
    spektrafilm::ColorSpace::BmdFilmWideGamutGen5,
    spektrafilm::ColorSpace::DavinciIntermediateWideGamut,
    spektrafilm::ColorSpace::RedLog3G10RedWideGamutRgb,
    spektrafilm::ColorSpace::SonySLog3SGamut3,
    spektrafilm::ColorSpace::CanonLog2CinemaGamutD55,
    spektrafilm::ColorSpace::CanonLog3CinemaGamutD55,
    spektrafilm::ColorSpace::PanasonicVLogVGamut,
    spektrafilm::ColorSpace::LinearRec2020,
    spektrafilm::ColorSpace::AcesCct,
    spektrafilm::ColorSpace::AcesCc,
    spektrafilm::ColorSpace::Srgb,
    spektrafilm::ColorSpace::ProPhotoRgb,
    spektrafilm::ColorSpace::AdobeRgb1998,
    spektrafilm::ColorSpace::P3D65Gamma26,
    spektrafilm::ColorSpace::Rec709Gamma22,
    spektrafilm::ColorSpace::Rec709Gamma24,
  };

  spektrafilm::RenderParams params{};
  switch (getIntAtTime(data->process, time, 0)) {
    case 1:
      params.process = spektrafilm::ProcessMode::ScanNegative;
      break;
    case 2:
      params.process = spektrafilm::ProcessMode::ProcessNegative;
      break;
    case 0:
    default:
      params.process = spektrafilm::ProcessMode::PrintSimulation;
      break;
  }
  params.rgbToRawMethod = rgbToRawMethodFromChoice(getIntAtTime(data->rgbToRawMethod, time, 0));
  if (params.process == spektrafilm::ProcessMode::ProcessNegative) {
    params.rgbToRawMethod = spektrafilm::RgbToRawMethod::Hanatos2026;
  }
  params.outputRole = outputRoleForFlavor(getIntAtTime(data->outputRole, time, 0));
  const int inputPrimariesIndex = std::clamp(
    getIntAtTime(data->inputPrimariesColorSpace, time, 1),
    0,
    static_cast<int>(std::size(kPrimariesColorSpaces) - 1u)
  );
  const int inputTransferIndex = std::clamp(
    getIntAtTime(data->inputTransferColorSpace, time, 1),
    0,
    static_cast<int>(std::size(kTransferColorSpaces) - 1u)
  );
  const int outputPrimariesIndex = std::clamp(
    getIntAtTime(data->outputPrimariesColorSpace, time, 12),
    0,
    static_cast<int>(std::size(kPrimariesColorSpaces) - 1u)
  );
  const int outputTransferIndex = std::clamp(
    getIntAtTime(data->outputTransferColorSpace, time, 17),
    0,
    static_cast<int>(std::size(kTransferColorSpaces) - 1u)
  );
  params.inputPrimariesColorSpace = kPrimariesColorSpaces[inputPrimariesIndex];
  params.inputTransferColorSpace = kTransferColorSpaces[inputTransferIndex];
  params.outputPrimariesColorSpace = kPrimariesColorSpaces[outputPrimariesIndex];
  params.outputTransferColorSpace = kTransferColorSpaces[outputTransferIndex];
  params.inputColorSpace = params.inputPrimariesColorSpace;
  params.outputColorSpace = params.outputPrimariesColorSpace;
  params.scanNegativeInvert = getBoolAtTime(data->scanNegativeInvert, time, false);
  params.hdrPreset = static_cast<spektrafilm::HdrPreset>(getIntAtTime(data->hdrPreset, time, 0));
  params.hdrTransfer = static_cast<spektrafilm::HdrTransfer>(getIntAtTime(data->hdrTransfer, time, 0));
  params.hdrReferenceWhiteNits = static_cast<float>(getDoubleAtTime(data->hdrReferenceWhiteNits, time, 203.0));
  params.hdrPeakNits = static_cast<float>(getDoubleAtTime(data->hdrPeakNits, time, 1000.0));
  params.hdrExposureEv = static_cast<float>(getDoubleAtTime(data->hdrExposureEv, time, 0.0));
  params.hdrToneMapping = static_cast<spektrafilm::HdrToneMapping>(getIntAtTime(data->hdrToneMapping, time, 1));
  params.colorAdaptation = getBoolAtTime(data->colorAdaptation, time, false);
  if (params.colorAdaptation && kPluginFlavor == PluginFlavor::Flow) {
    params.colorAdaptationInputCompression = true;
    params.colorAdaptationCurveSmoothing = true;
    params.colorAdaptationOutputLightnessCompression = true;
    params.colorAdaptationOutputChromaCompression = true;
  } else {
    params.colorAdaptationInputCompression = getBoolAtTime(data->colorAdaptationInputCompression, time, true);
    params.colorAdaptationCurveSmoothing = getBoolAtTime(data->colorAdaptationCurveSmoothing, time, true);
    params.colorAdaptationOutputLightnessCompression = getBoolAtTime(data->colorAdaptationOutputLightnessCompression, time, true);
    params.colorAdaptationOutputChromaCompression = getBoolAtTime(data->colorAdaptationOutputChromaCompression, time, true);
  }
  params.cameraUvFilterEnabled = getBoolAtTime(data->cameraUvFilterEnabled, time, false);
  params.cameraUvCutNm = static_cast<float>(getDoubleAtTime(data->cameraUvCutNm, time, 410.0));
  params.cameraIrFilterEnabled = getBoolAtTime(data->cameraIrFilterEnabled, time, false);
  params.cameraIrCutNm = static_cast<float>(getDoubleAtTime(data->cameraIrCutNm, time, 675.0));
  params.film = getIntAtTime(data->film, time, static_cast<int>(spektrafilm::kSpektraDefaultFilmIndex));
  params.paper = getIntAtTime(data->paper, time, static_cast<int>(spektrafilm::kSpektraDefaultPaperIndex));
  const int printTiming = spektrafilm::kSpektraAcademyPrinterDensityEnabled
    ? getIntAtTime(data->printTiming, time, 0)
    : static_cast<int>(spektrafilm::PrintTimingMode::FilteredEnlarger);
  params.printTiming = static_cast<spektrafilm::PrintTimingMode>(printTiming);
  params.printSource = static_cast<spektrafilm::PrintSourceMode>(
    std::clamp(getIntAtTime(data->printSource, time, 0), 0, 1)
  );
  if (isProductScan()) {
    params.process = spektrafilm::ProcessMode::PrintSimulation;
    params.printSource = spektrafilm::PrintSourceMode::ScannedNegativeBypass;
  }
  params.scanInputEncoding = static_cast<spektrafilm::ScanInputEncoding>(
    std::clamp(getIntAtTime(data->scanInputEncoding, time, 0), 0, 3)
  );
  const int scanInputColorIndex = std::clamp(
    getIntAtTime(data->scanInputColorSpace, time, 0),
    0,
    static_cast<int>(std::size(kScanInputColorSpaces) - 1u)
  );
  params.scanInputColorSpace = kScanInputColorSpaces[scanInputColorIndex];
  const int scanWorkingColorIndex = std::clamp(
    getIntAtTime(data->scanWorkingColorSpace, time, 0),
    0,
    static_cast<int>(std::size(kScanWorkingColorSpaces) - 1u)
  );
  params.scanWorkingColorSpace = kScanWorkingColorSpaces[scanWorkingColorIndex];
  params.scanDensityBasis = spektrafilm::ScanDensityBasis::NeutralCmy;
  double scanFilmBaseRgb[3] = {1.0, 1.0, 1.0};
  getDouble3DAtTime(data->scanFilmBaseRgb, time, scanFilmBaseRgb, scanFilmBaseRgb);
  params.scanFilmBaseR = static_cast<float>(scanFilmBaseRgb[0]);
  params.scanFilmBaseG = static_cast<float>(scanFilmBaseRgb[1]);
  params.scanFilmBaseB = static_cast<float>(scanFilmBaseRgb[2]);
  double scanFilmBaseColorRgb[3] = {1.0, 0.78, 0.58};
  getDouble3DAtTime(data->scanFilmBaseColorRgb, time, scanFilmBaseColorRgb, scanFilmBaseColorRgb);
  params.scanFilmBaseColorR = static_cast<float>(scanFilmBaseColorRgb[0]);
  params.scanFilmBaseColorG = static_cast<float>(scanFilmBaseColorRgb[1]);
  params.scanFilmBaseColorB = static_cast<float>(scanFilmBaseColorRgb[2]);
  params.scanFilmBaseTemp = static_cast<float>(getDoubleAtTime(data->scanFilmBaseTemp, time, 0.0));
  params.scanFilmBaseTint = static_cast<float>(getDoubleAtTime(data->scanFilmBaseTint, time, 0.0));
  double scanBlackFlareRgb[3] = {0.0, 0.0, 0.0};
  getDouble3DAtTime(data->scanBlackFlareRgb, time, scanBlackFlareRgb, scanBlackFlareRgb);
  params.scanBlackFlareR = static_cast<float>(scanBlackFlareRgb[0]);
  params.scanBlackFlareG = static_cast<float>(scanBlackFlareRgb[1]);
  params.scanBlackFlareB = static_cast<float>(scanBlackFlareRgb[2]);
  params.scanExposureEv = static_cast<float>(getDoubleAtTime(data->scanExposureEv, time, 0.0));
  params.scanDensityContrast = static_cast<float>(getDoubleAtTime(data->scanDensityContrast, time, 1.5));
  double scanDensityScaleRgb[3] = {1.0, 1.0, 1.0};
  getDouble3DAtTime(data->scanDensityScaleRgb, time, scanDensityScaleRgb, scanDensityScaleRgb);
  params.scanDensityScaleR = static_cast<float>(scanDensityScaleRgb[0]);
  params.scanDensityScaleG = static_cast<float>(scanDensityScaleRgb[1]);
  params.scanDensityScaleB = static_cast<float>(scanDensityScaleRgb[2]);
  if (isProductScan()) {
    params.scanDensityContrast = 1.5f;
    params.scanDensityScaleR = static_cast<float>(getDoubleAtTime(data->scanDensityScaleR, time, 1.0));
    params.scanDensityScaleG = static_cast<float>(getDoubleAtTime(data->scanDensityScaleG, time, 1.0));
    params.scanDensityScaleB = static_cast<float>(getDoubleAtTime(data->scanDensityScaleB, time, 1.0));
  }
  double scanDensityOffsetRgb[3] = {0.0, 0.0, 0.0};
  getDouble3DAtTime(data->scanDensityOffsetRgb, time, scanDensityOffsetRgb, scanDensityOffsetRgb);
  params.scanDensityOffsetR = static_cast<float>(scanDensityOffsetRgb[0]);
  params.scanDensityOffsetG = static_cast<float>(scanDensityOffsetRgb[1]);
  params.scanDensityOffsetB = static_cast<float>(scanDensityOffsetRgb[2]);
  params.filmExposureEv = static_cast<float>(getDoubleAtTime(data->filmExposureEv, time, 0.0));
  params.autoExposure = getBoolAtTime(data->autoExposure, time, false);
  params.autoExposureMethod = static_cast<spektrafilm::AutoExposureMethod>(getIntAtTime(data->autoExposureMethod, time, 0));
  params.printExposureEv = static_cast<float>(getDoubleAtTime(data->printExposureEv, time, 0.0));
  params.filmPushPullMode = static_cast<spektrafilm::PushPullMode>(getIntAtTime(data->filmPushPullMode, time, 0));
  params.filmPushPullStops = static_cast<float>(getDoubleAtTime(data->filmPushPullStops, time, 0.0));
  params.printPushPullStops = static_cast<float>(getDoubleAtTime(data->printPushPullStops, time, 0.0));
  params.negativeBleachBypassAmount = static_cast<float>(getDoubleAtTime(data->negativeBleachBypassAmount, time, 0.0));
  params.negativeLeucoCyanCoupling = static_cast<float>(getDoubleAtTime(data->negativeLeucoCyanCoupling, time, 1.0));
  params.printBleachBypassAmount = static_cast<float>(getDoubleAtTime(data->printBleachBypassAmount, time, 0.0));
  params.filmGamma = static_cast<float>(getDoubleAtTime(data->filmGamma, time, 1.0));
  params.printGamma = static_cast<float>(getDoubleAtTime(data->printGamma, time, 1.0));
  params.printShadowShape = static_cast<float>(getDoubleAtTime(data->printShadowShape, time, 0.0));
  params.printHighlightShape = static_cast<float>(getDoubleAtTime(data->printHighlightShape, time, 0.0));
  params.filterC = static_cast<float>(getDoubleAtTime(data->filterC, time, 0.0));
  params.filterMShift = static_cast<float>(getDoubleAtTime(data->filterMShift, time, 0.0));
  params.filterYShift = static_cast<float>(getDoubleAtTime(data->filterYShift, time, 0.0));
  params.enlargerScale = static_cast<float>(getDoubleAtTime(data->enlargerScale, time, 1.0));
  params.enlargerOffsetXPercent = static_cast<float>(getDoubleAtTime(data->enlargerOffsetXPercent, time, 0.0));
  params.enlargerOffsetYPercent = static_cast<float>(getDoubleAtTime(data->enlargerOffsetYPercent, time, 0.0));
  params.preflashExposure = static_cast<float>(getDoubleAtTime(data->preflashExposure, time, 0.0));
  params.preflashMFilterShift = static_cast<float>(getDoubleAtTime(data->preflashMFilterShift, time, 0.0));
  params.preflashYFilterShift = static_cast<float>(getDoubleAtTime(data->preflashYFilterShift, time, 0.0));
  params.printerLightsR = static_cast<float>(getDoubleAtTime(data->printerLightR, time, 0.0));
  params.printerLightsG = static_cast<float>(getDoubleAtTime(data->printerLightG, time, 0.0));
  params.printerLightsB = static_cast<float>(getDoubleAtTime(data->printerLightB, time, 0.0));
  params.printerLightsGang = getBoolAtTime(data->printerLightsGang, time, false);
  params.printerLightCalibration = getBoolAtTime(data->printerLightCalibration, time, true);
  params.dirCouplersAmount = static_cast<float>(getDoubleAtTime(data->dirAmount, time, 0.0));
  params.dirCouplersDiffusionUm = static_cast<float>(getDoubleAtTime(data->dirDiffusionUm, time, 20.0));
  params.dirCouplersDiffusionTailUm = static_cast<float>(getDoubleAtTime(data->dirDiffusionTailUm, time, 200.0));
  params.dirCouplersDiffusionTailWeight = static_cast<float>(getDoubleAtTime(data->dirDiffusionTailWeight, time, 0.06));
  params.dirCouplersInhibitionSameLayer = static_cast<float>(getDoubleAtTime(data->dirInhibitionSameLayer, time, 1.0));
  params.dirCouplersInhibitionInterlayer = static_cast<float>(getDoubleAtTime(data->dirInhibitionInterlayer, time, 1.0));
  double dirGammaSameLayerRgb[3] = {0.336, 0.319, 0.273};
  if (data->dirGammaSameLayerRgb) {
    gParamHost->paramGetValueAtTime(
      data->dirGammaSameLayerRgb,
      time,
      &dirGammaSameLayerRgb[0],
      &dirGammaSameLayerRgb[1],
      &dirGammaSameLayerRgb[2]
    );
  }
  double dirGammaRToGb[2] = {0.353, 0.302};
  if (data->dirGammaRToGb) {
    gParamHost->paramGetValueAtTime(data->dirGammaRToGb, time, &dirGammaRToGb[0], &dirGammaRToGb[1]);
  }
  double dirGammaGToRb[2] = {0.154, 0.353};
  if (data->dirGammaGToRb) {
    gParamHost->paramGetValueAtTime(data->dirGammaGToRb, time, &dirGammaGToRb[0], &dirGammaGToRb[1]);
  }
  double dirGammaBToRg[2] = {0.168, 0.226};
  if (data->dirGammaBToRg) {
    gParamHost->paramGetValueAtTime(data->dirGammaBToRg, time, &dirGammaBToRg[0], &dirGammaBToRg[1]);
  }
  params.dirCouplersGammaSameLayerR = static_cast<float>(dirGammaSameLayerRgb[0]);
  params.dirCouplersGammaSameLayerG = static_cast<float>(dirGammaSameLayerRgb[1]);
  params.dirCouplersGammaSameLayerB = static_cast<float>(dirGammaSameLayerRgb[2]);
  params.dirCouplersGammaRToG = static_cast<float>(dirGammaRToGb[0]);
  params.dirCouplersGammaRToB = static_cast<float>(dirGammaRToGb[1]);
  params.dirCouplersGammaGToR = static_cast<float>(dirGammaGToRb[0]);
  params.dirCouplersGammaGToB = static_cast<float>(dirGammaGToRb[1]);
  params.dirCouplersGammaBToR = static_cast<float>(dirGammaBToRg[0]);
  params.dirCouplersGammaBToG = static_cast<float>(dirGammaBToRg[1]);
  params.grainEnabled = getBoolAtTime(data->grainEnabled, time, false);
  params.grainModel = static_cast<spektrafilm::GrainModel>(getIntAtTime(data->grainModel, time, 0));
  if (!flavorAllowsDevelopmentControls() && params.grainModel == spektrafilm::GrainModel::GrainSynthesis) {
    params.grainModel = spektrafilm::GrainModel::Preview;
  }
  params.filmFormat = static_cast<spektrafilm::FilmFormat>(getIntAtTime(data->filmFormat, time, 4));
  params.grainAmount = static_cast<float>(getDoubleAtTime(data->grainAmount, time, 1.0));
  params.grainSaturation = static_cast<float>(getDoubleAtTime(data->grainSaturation, time, 1.0));
  params.grainSublayersEnabled = getBoolAtTime(data->grainSublayersEnabled, time, true);
  params.grainSubLayerCount = getIntAtTime(data->grainSubLayerCount, time, 1);
  params.grainParticleAreaUm2 = static_cast<float>(getDoubleAtTime(data->grainParticleAreaUm2, time, 0.1));
  double grainParticleScale[3] = {1.2, 1.0, 2.5};
  if (data->grainParticleScale) {
    gParamHost->paramGetValueAtTime(data->grainParticleScale, time, &grainParticleScale[0], &grainParticleScale[1], &grainParticleScale[2]);
  }
  params.grainParticleScaleR = static_cast<float>(grainParticleScale[0]);
  params.grainParticleScaleG = static_cast<float>(grainParticleScale[1]);
  params.grainParticleScaleB = static_cast<float>(grainParticleScale[2]);
  double grainParticleScaleLayers[3] = {6.0, 1.0, 0.4};
  if (data->grainParticleScaleLayers) {
    gParamHost->paramGetValueAtTime(data->grainParticleScaleLayers, time, &grainParticleScaleLayers[0], &grainParticleScaleLayers[1], &grainParticleScaleLayers[2]);
  }
  params.grainParticleScaleLayer0 = static_cast<float>(grainParticleScaleLayers[0]);
  params.grainParticleScaleLayer1 = static_cast<float>(grainParticleScaleLayers[1]);
  params.grainParticleScaleLayer2 = static_cast<float>(grainParticleScaleLayers[2]);
  double grainDensityMin[3] = {0.04, 0.05, 0.06};
  if (data->grainDensityMin) {
    gParamHost->paramGetValueAtTime(data->grainDensityMin, time, &grainDensityMin[0], &grainDensityMin[1], &grainDensityMin[2]);
  }
  params.grainDensityMinR = static_cast<float>(grainDensityMin[0]);
  params.grainDensityMinG = static_cast<float>(grainDensityMin[1]);
  params.grainDensityMinB = static_cast<float>(grainDensityMin[2]);
  double grainUniformity[3] = {0.99, 0.97, 0.98};
  if (data->grainUniformity) {
    gParamHost->paramGetValueAtTime(data->grainUniformity, time, &grainUniformity[0], &grainUniformity[1], &grainUniformity[2]);
  }
  params.grainUniformityR = static_cast<float>(grainUniformity[0]);
  params.grainUniformityG = static_cast<float>(grainUniformity[1]);
  params.grainUniformityB = static_cast<float>(grainUniformity[2]);
  params.grainFinalBlurUm = static_cast<float>(getDoubleAtTime(data->grainFinalBlurUm, time, 7.17));
  params.grainBlurDyeCloudsUm = static_cast<float>(getDoubleAtTime(data->grainBlurDyeCloudsUm, time, 1.0));
  double microStructure[2] = {0.2, 30.0};
  if (data->grainMicroStructure) {
    gParamHost->paramGetValueAtTime(data->grainMicroStructure, time, &microStructure[0], &microStructure[1]);
  }
  params.grainMicroStructureScale = static_cast<float>(microStructure[0]);
  params.grainMicroStructureSigmaNm = static_cast<float>(microStructure[1]);
  params.grainSeed = static_cast<uint32_t>(getIntAtTime(data->grainSeed, time, 1));
  params.grainAnimate = getBoolAtTime(data->grainAnimate, time, false);
  params.grainSynthesisSize = static_cast<float>(getDoubleAtTime(data->grainSynthesisSize, time, 1.0));
  params.grainSynthesisAmount = static_cast<float>(getDoubleAtTime(data->grainSynthesisAmount, time, 1.0));
  params.grainSynthesisSharpness = static_cast<float>(getDoubleAtTime(data->grainSynthesisSharpness, time, 1.0));
  params.grainSynthesisQuality = static_cast<float>(getDoubleAtTime(data->grainSynthesisQuality, time, 1.0));
  params.grainSynthesisSamples = getIntAtTime(data->grainSynthesisSamples, time, 128);
  params.grainSynthesisMeanRadiusUm = static_cast<float>(getDoubleAtTime(data->grainSynthesisMeanRadiusUm, time, 0.25));
  params.grainSynthesisRadiusStdDevRatio = static_cast<float>(getDoubleAtTime(data->grainSynthesisRadiusStdDevRatio, time, 0.0));
  params.grainSynthesisObservationSigmaUm = static_cast<float>(getDoubleAtTime(data->grainSynthesisObservationSigmaUm, time, 1.0));
  params.grainSynthesisCellSizeRatio = static_cast<float>(getDoubleAtTime(data->grainSynthesisCellSizeRatio, time, 1.0));
  params.grainSynthesisMaxRadiusQuantile = static_cast<float>(getDoubleAtTime(data->grainSynthesisMaxRadiusQuantile, time, 0.999));
  params.grainSynthesisCoverageEpsilon = static_cast<float>(getDoubleAtTime(data->grainSynthesisCoverageEpsilon, time, 0.0001));
  params.grainSynthesisMaxGrainsPerCell = getIntAtTime(data->grainSynthesisMaxGrainsPerCell, time, 32);
  double grainSynthesisRadiusScale[3] = {1.2, 1.0, 2.5};
  if (data->grainSynthesisRadiusScale) {
    gParamHost->paramGetValueAtTime(data->grainSynthesisRadiusScale, time, &grainSynthesisRadiusScale[0], &grainSynthesisRadiusScale[1], &grainSynthesisRadiusScale[2]);
  }
  params.grainSynthesisRadiusScaleR = static_cast<float>(grainSynthesisRadiusScale[0]);
  params.grainSynthesisRadiusScaleG = static_cast<float>(grainSynthesisRadiusScale[1]);
  params.grainSynthesisRadiusScaleB = static_cast<float>(grainSynthesisRadiusScale[2]);
  double grainSynthesisLayerScale[3] = {6.0, 1.0, 0.4};
  if (data->grainSynthesisLayerScale) {
    gParamHost->paramGetValueAtTime(data->grainSynthesisLayerScale, time, &grainSynthesisLayerScale[0], &grainSynthesisLayerScale[1], &grainSynthesisLayerScale[2]);
  }
  params.grainSynthesisLayerScale0 = static_cast<float>(grainSynthesisLayerScale[0]);
  params.grainSynthesisLayerScale1 = static_cast<float>(grainSynthesisLayerScale[1]);
  params.grainSynthesisLayerScale2 = static_cast<float>(grainSynthesisLayerScale[2]);
  params.grainSynthesisLayered = getBoolAtTime(data->grainSynthesisLayered, time, true);
  params.halationEnabled = getBoolAtTime(data->halationEnabled, time, false);
  params.scatterAmount = static_cast<float>(getDoubleAtTime(data->scatterAmount, time, 1.0));
  params.scatterScale = static_cast<float>(getDoubleAtTime(data->scatterScale, time, 1.0));
  params.halationAmount = static_cast<float>(getDoubleAtTime(data->halationAmount, time, 1.0));
  params.halationScale = static_cast<float>(getDoubleAtTime(data->halationScale, time, 1.0));
  double strength[3] = {0.05, 0.015, 0.0};
  if (data->halationStrength) {
    gParamHost->paramGetValueAtTime(data->halationStrength, time, &strength[0], &strength[1], &strength[2]);
  }
  params.halationStrengthR = static_cast<float>(strength[0]);
  params.halationStrengthG = static_cast<float>(strength[1]);
  params.halationStrengthB = static_cast<float>(strength[2]);
  params.halationBoostEv = static_cast<float>(getDoubleAtTime(data->halationBoostEv, time, 0.0));
  params.halationBoostRange = static_cast<float>(getDoubleAtTime(data->halationBoostRange, time, 0.3));
  params.halationProtectEv = static_cast<float>(getDoubleAtTime(data->halationProtectEv, time, 4.0));
  params.cameraDiffusionEnabled = getBoolAtTime(data->cameraDiffusionEnabled, time, false);
  params.cameraDiffusionFamily = static_cast<spektrafilm::DiffusionFilterFamily>(getIntAtTime(data->cameraDiffusionFamily, time, 1));
  params.cameraDiffusionStrength = static_cast<float>(getDoubleAtTime(data->cameraDiffusionStrength, time, 0.5));
  params.cameraDiffusionSpatialScale = static_cast<float>(getDoubleAtTime(data->cameraDiffusionSpatialScale, time, 1.0));
  params.cameraDiffusionHaloWarmth = static_cast<float>(getDoubleAtTime(data->cameraDiffusionHaloWarmth, time, 0.0));
  params.cameraDiffusionCoreIntensity = static_cast<float>(getDoubleAtTime(data->cameraDiffusionCoreIntensity, time, 1.0));
  params.cameraDiffusionCoreSize = static_cast<float>(getDoubleAtTime(data->cameraDiffusionCoreSize, time, 1.0));
  params.cameraDiffusionHaloIntensity = static_cast<float>(getDoubleAtTime(data->cameraDiffusionHaloIntensity, time, 1.0));
  params.cameraDiffusionHaloSize = static_cast<float>(getDoubleAtTime(data->cameraDiffusionHaloSize, time, 1.0));
  params.cameraDiffusionBloomIntensity = static_cast<float>(getDoubleAtTime(data->cameraDiffusionBloomIntensity, time, 1.0));
  params.cameraDiffusionBloomSize = static_cast<float>(getDoubleAtTime(data->cameraDiffusionBloomSize, time, 1.0));
  params.printDiffusionEnabled = getBoolAtTime(data->printDiffusionEnabled, time, false);
  params.printDiffusionFamily = static_cast<spektrafilm::DiffusionFilterFamily>(getIntAtTime(data->printDiffusionFamily, time, 1));
  params.printDiffusionStrength = static_cast<float>(getDoubleAtTime(data->printDiffusionStrength, time, 0.5));
  params.printDiffusionSpatialScale = static_cast<float>(getDoubleAtTime(data->printDiffusionSpatialScale, time, 1.0));
  params.printDiffusionHaloWarmth = static_cast<float>(getDoubleAtTime(data->printDiffusionHaloWarmth, time, 0.0));
  params.printDiffusionCoreIntensity = static_cast<float>(getDoubleAtTime(data->printDiffusionCoreIntensity, time, 1.0));
  params.printDiffusionCoreSize = static_cast<float>(getDoubleAtTime(data->printDiffusionCoreSize, time, 1.0));
  params.printDiffusionHaloIntensity = static_cast<float>(getDoubleAtTime(data->printDiffusionHaloIntensity, time, 1.0));
  params.printDiffusionHaloSize = static_cast<float>(getDoubleAtTime(data->printDiffusionHaloSize, time, 1.0));
  params.printDiffusionBloomIntensity = static_cast<float>(getDoubleAtTime(data->printDiffusionBloomIntensity, time, 1.0));
  params.printDiffusionBloomSize = static_cast<float>(getDoubleAtTime(data->printDiffusionBloomSize, time, 1.0));
  params.scannerEnabled = getBoolAtTime(data->scannerEnabled, time, false);
  params.scannerWhiteCorrection = getBoolAtTime(data->scannerWhiteCorrection, time, false);
  params.scannerBlackCorrection = getBoolAtTime(data->scannerBlackCorrection, time, false);
  params.scannerWhiteLevel = static_cast<float>(getDoubleAtTime(data->scannerWhiteLevel, time, 0.98));
  params.scannerBlackLevel = static_cast<float>(getDoubleAtTime(data->scannerBlackLevel, time, 0.01));
  params.glarePercent = static_cast<float>(getDoubleAtTime(data->glarePercent, time, 0.03));
  params.glareRoughness = static_cast<float>(getDoubleAtTime(data->glareRoughness, time, 0.7));
  params.glareBlur = static_cast<float>(getDoubleAtTime(data->glareBlur, time, 0.5));
  params.scannerMtf50LpMm = static_cast<float>(getDoubleAtTime(data->scannerMtf50LpMm, time, 60.0));
  params.scannerUnsharpRadiusUm = static_cast<float>(getDoubleAtTime(data->scannerUnsharpRadiusUm, time, 5.0));
  params.scannerUnsharpAmount = static_cast<float>(getDoubleAtTime(data->scannerUnsharpAmount, time, 0.7));
  params.gpuRenderTiling = getIntAtTime(data->gpuRenderTiling, time, 0) == 1
    ? spektrafilm::GpuRenderTilingMode::Tiled
    : spektrafilm::GpuRenderTilingMode::LegacyFullFrame;
  const bool scannedNegativeBypass =
    params.process == spektrafilm::ProcessMode::PrintSimulation &&
    params.printSource == spektrafilm::PrintSourceMode::ScannedNegativeBypass;
  if (scannedNegativeBypass) {
    params.film = static_cast<int32_t>(spektrafilm::kSpektraDefaultFilmIndex);
    params.filmFormat = spektrafilm::FilmFormat::Standard35;
    params.rgbToRawMethod = spektrafilm::RgbToRawMethod::Hanatos2026;
    params.cameraUvFilterEnabled = false;
    params.cameraIrFilterEnabled = false;
    params.filmExposureEv = 0.0f;
    params.autoExposure = false;
    params.filmPushPullMode = spektrafilm::PushPullMode::Standard;
    params.filmPushPullStops = 0.0f;
    params.negativeBleachBypassAmount = 0.0f;
    params.negativeLeucoCyanCoupling = 1.0f;
    params.filmGamma = 1.0f;
    params.dirCouplersAmount = 0.0f;
    params.grainEnabled = false;
    params.halationEnabled = false;
    params.cameraDiffusionEnabled = false;
  }
  if (params.process == spektrafilm::ProcessMode::ProcessNegative) {
    params.rgbToRawMethod = spektrafilm::RgbToRawMethod::Hanatos2026;
    params.film = static_cast<int32_t>(spektrafilm::kSpektraDefaultFilmIndex);
    params.filmFormat = spektrafilm::FilmFormat::Standard35;
    params.cameraUvFilterEnabled = false;
    params.cameraIrFilterEnabled = false;
    params.filmExposureEv = 0.0f;
    params.autoExposure = false;
    params.filmPushPullMode = spektrafilm::PushPullMode::Standard;
    params.filmPushPullStops = 0.0f;
    params.negativeBleachBypassAmount = 0.0f;
    params.negativeLeucoCyanCoupling = 1.0f;
    params.filmGamma = 1.0f;
    params.enlargerScale = 1.0f;
    params.enlargerOffsetXPercent = 0.0f;
    params.enlargerOffsetYPercent = 0.0f;
    params.dirCouplersAmount = 0.0f;
    params.grainEnabled = false;
    params.halationEnabled = false;
    params.cameraDiffusionEnabled = false;
  }
  if (isProProductionBuild()) {
    if (!isProductScan()) {
      params.film = productionNegativeIndexFromChoice(getIntAtTime(data->productionProfileNegative, time, 0));
    }
    params.paper = productionPrintIndexFromChoice(getIntAtTime(data->productionProfilePrint, time, 0));
    forceProductionRenderParams(params);
    applyBundledProductionCalibration(params);
    if (!isProductScan() && getBoolAtTime(data->productionPrinterLightsEnabled, time, true)) {
      applyCreativePrinterLightTrims(
        params,
        static_cast<float>(getDoubleAtTime(data->creativePrinterLightR, time, 0.0)),
        static_cast<float>(getDoubleAtTime(data->creativePrinterLightG, time, 0.0)),
        static_cast<float>(getDoubleAtTime(data->creativePrinterLightB, time, 0.0))
      );
    }
  }
  return params;
}

bool validStoredKind(ParamValueKind kind) {
  switch (kind) {
    case ParamValueKind::Int:
    case ParamValueKind::Bool:
    case ParamValueKind::Double:
    case ParamValueKind::Double2D:
    case ParamValueKind::Double3D:
      return true;
  }
  return false;
}

std::string encodeDefaultsSnapshot(const DefaultsSnapshot &snapshot) {
  std::ostringstream out;
  out << "SPKDFLT2\n";
  for (const auto &item : snapshot) {
    const std::string &name = item.first;
    const StoredParamValue &value = item.second;
    const int components = paramComponentCount(value.kind);
    out << name << ' ' << static_cast<int>(value.kind) << ' ' << components;
    if (paramKindUsesDouble(value.kind)) {
      out << std::setprecision(std::numeric_limits<double>::max_digits10);
      for (int i = 0; i < components; ++i) {
        out << ' ' << value.doubleValue[i];
      }
    } else {
      for (int i = 0; i < components; ++i) {
        out << ' ' << value.intValue[i];
      }
    }
    out << '\n';
  }
  return out.str();
}

bool decodeDefaultsSnapshot(const std::string &text, DefaultsSnapshot &snapshot) {
  std::istringstream in(text);
  std::string line;
  if (!std::getline(in, line) || line != "SPKDFLT2") {
    return false;
  }
  DefaultsSnapshot decoded;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::istringstream row(line);
    std::string name;
    int kindRaw = 0;
    int components = 0;
    if (!(row >> name >> kindRaw >> components)) {
      continue;
    }
    StoredParamValue value{};
    value.kind = static_cast<ParamValueKind>(kindRaw);
    if (!validStoredKind(value.kind) || components != paramComponentCount(value.kind)) {
      continue;
    }
    const ParamDefault *factory = defaultForParam(name.c_str());
    if (!factory || factory->kind != value.kind) {
      continue;
    }
    bool ok = true;
    if (paramKindUsesDouble(value.kind)) {
      for (int c = 0; c < components; ++c) {
        if (!(row >> value.doubleValue[c])) {
          ok = false;
          break;
        }
      }
    } else {
      for (int c = 0; c < components; ++c) {
        if (!(row >> value.intValue[c])) {
          ok = false;
          break;
        }
      }
    }
    if (!ok) {
      continue;
    }
    decoded[name] = value;
  }
  snapshot = std::move(decoded);
  return true;
}

std::filesystem::path userDefaultsPath() {
#if defined _WIN32
  const char *base = std::getenv("APPDATA");
  if (base && base[0]) {
    return std::filesystem::path(base) / "MCLookFilmLab" / "defaults-v1.mclfdefaults";
  }
  const char *home = std::getenv("USERPROFILE");
  return std::filesystem::path(home && home[0] ? home : ".") / "AppData" / "Roaming" / "MCLookFilmLab" / "defaults-v1.mclfdefaults";
#elif defined __APPLE__
  const char *home = std::getenv("HOME");
  return std::filesystem::path(home && home[0] ? home : ".") /
    "Library" / "Application Support" / "MCLookFilmLab" / "defaults-v1.mclfdefaults";
#else
  const char *home = std::getenv("HOME");
  const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
  const std::filesystem::path configRoot =
    xdgConfigHome && xdgConfigHome[0]
      ? std::filesystem::path(xdgConfigHome)
      : std::filesystem::path(home && home[0] ? home : ".") / ".config";
  return configRoot / "MCLookFilmLab" / "defaults-v1.mclfdefaults";
#endif
}

std::filesystem::path legacyUserDefaultsPath() {
#if defined _WIN32
  const char *base = std::getenv("APPDATA");
  if (base && base[0]) {
    return std::filesystem::path(base) / "spektrafilm" / "ofx-defaults-v1.spkdefaults";
  }
  const char *home = std::getenv("USERPROFILE");
  return std::filesystem::path(home && home[0] ? home : ".") / "AppData" / "Roaming" / "spektrafilm" / "ofx-defaults-v1.spkdefaults";
#elif defined __APPLE__
  const char *home = std::getenv("HOME");
  return std::filesystem::path(home && home[0] ? home : ".") /
    "Library" / "Application Support" / "spektrafilm" / "ofx-defaults-v1.spkdefaults";
#else
  const char *home = std::getenv("HOME");
  const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
  const std::filesystem::path configRoot =
    xdgConfigHome && xdgConfigHome[0]
      ? std::filesystem::path(xdgConfigHome)
      : std::filesystem::path(home && home[0] ? home : ".") / ".config";
  return configRoot / "spektrafilm" / "ofx-defaults-v1.spkdefaults";
#endif
}

void obfuscateDefaultsText(std::string &text) {
  constexpr uint8_t key[] = {
    0x53, 0x70, 0x65, 0x6b, 0x74, 0x72, 0x61, 0x46,
    0x69, 0x6c, 0x6d, 0x4f, 0x46, 0x58, 0x31, 0x21
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const uint8_t stream = static_cast<uint8_t>(key[i % sizeof(key)] + static_cast<uint8_t>((i * 37u) & 0xffu));
    text[i] = static_cast<char>(static_cast<uint8_t>(text[i]) ^ stream);
  }
}

bool loadSnapshotFromFile(const std::filesystem::path &path, DefaultsSnapshot &snapshot, bool &found, std::string &error) {
  found = false;
  error.clear();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return true;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open LookFilmLab defaults file for reading: " + path.string();
    return false;
  }
  std::string text{
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  };
  if (!input.good() && !input.eof()) {
    error = "Could not read LookFilmLab defaults file: " + path.string();
    return false;
  }
  obfuscateDefaultsText(text);
  DefaultsSnapshot decoded;
  if (!decodeDefaultsSnapshot(text, decoded)) {
    error = "LookFilmLab defaults file is not a recognized defaults snapshot: " + path.string();
    return false;
  }
  snapshot = std::move(decoded);
  found = true;
  return true;
}

bool saveSnapshotToFile(const std::filesystem::path &path, const DefaultsSnapshot &snapshot, std::string &error) {
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "Could not create LookFilmLab defaults folder: " + path.parent_path().string();
    return false;
  }
  std::string text = encodeDefaultsSnapshot(snapshot);
  obfuscateDefaultsText(text);
  const std::filesystem::path tempPath = path.string() + ".tmp";
  {
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "Could not open LookFilmLab defaults file for writing: " + tempPath.string();
      return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
      error = "Could not write LookFilmLab defaults file: " + tempPath.string();
      return false;
    }
  }
  std::filesystem::rename(tempPath, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
  }
  if (ec) {
    error = "Could not replace LookFilmLab defaults file: " + path.string();
    return false;
  }
  return true;
}

bool deleteSnapshotFile(const std::filesystem::path &path, std::string &error) {
  error.clear();
  std::error_code ec;
  std::filesystem::remove(path, ec);
  if (ec) {
    error = "Could not delete LookFilmLab defaults file: " + path.string();
    return false;
  }
  return true;
}

bool loadDefaultsFromFile(DefaultsSnapshot &snapshot, bool &found, std::string &error) {
  if (!loadSnapshotFromFile(userDefaultsPath(), snapshot, found, error) || found) {
    return error.empty() || found;
  }
  return loadSnapshotFromFile(legacyUserDefaultsPath(), snapshot, found, error);
}

bool saveDefaultsToFile(const DefaultsSnapshot &snapshot, std::string &error) {
  return saveSnapshotToFile(userDefaultsPath(), snapshot, error);
}

bool deleteDefaultsFile(std::string &error) {
  const bool deletedNew = deleteSnapshotFile(userDefaultsPath(), error);
  if (!deletedNew) {
    return false;
  }
  std::string legacyError;
  if (!deleteSnapshotFile(legacyUserDefaultsPath(), legacyError)) {
    error = legacyError;
    return false;
  }
  return true;
}

#if defined __APPLE__
std::string clipboardStatusMessage(const char *prefix, OSStatus status) {
  return std::string(prefix) + " (clipboard status " + std::to_string(static_cast<long long>(status)) + ").";
}

bool writeTextToClipboard(const std::string &text, std::string &error) {
  error.clear();
  PasteboardRef pasteboard = nullptr;
  OSStatus status = PasteboardCreate(kPasteboardClipboard, &pasteboard);
  if (status != noErr || !pasteboard) {
    error = clipboardStatusMessage("Could not open system clipboard", status);
    return false;
  }
  status = PasteboardClear(pasteboard);
  if (status == noErr) {
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(text.data()), static_cast<CFIndex>(text.size()));
    if (data) {
      PasteboardItemID itemId = reinterpret_cast<PasteboardItemID>(1);
      status = PasteboardPutItemFlavor(pasteboard, itemId, CFSTR("com.mcplugins.lookfilmlab.params"), data, 0);
      if (status == noErr) {
        status = PasteboardPutItemFlavor(pasteboard, itemId, CFSTR("public.utf8-plain-text"), data, 0);
      }
      CFRelease(data);
    } else {
      status = memFullErr;
    }
  }
  CFRelease(pasteboard);
  if (status != noErr) {
    error = clipboardStatusMessage("Could not write LookFilmLab params to system clipboard", status);
    return false;
  }
  return true;
}

bool copyClipboardFlavor(PasteboardRef pasteboard, PasteboardItemID item, CFStringRef flavor, std::string &text) {
  CFDataRef data = nullptr;
  const OSStatus status = PasteboardCopyItemFlavorData(pasteboard, item, flavor, &data);
  if (status != noErr || !data) {
    return false;
  }
  const UInt8 *bytes = CFDataGetBytePtr(data);
  const CFIndex length = CFDataGetLength(data);
  text.assign(reinterpret_cast<const char *>(bytes), static_cast<size_t>(length));
  CFRelease(data);
  return true;
}

bool readTextFromClipboard(std::string &text, bool &found, std::string &error) {
  text.clear();
  found = false;
  error.clear();
  PasteboardRef pasteboard = nullptr;
  OSStatus status = PasteboardCreate(kPasteboardClipboard, &pasteboard);
  if (status != noErr || !pasteboard) {
    error = clipboardStatusMessage("Could not open system clipboard", status);
    return false;
  }
  PasteboardSynchronize(pasteboard);
  ItemCount itemCount = 0;
  status = PasteboardGetItemCount(pasteboard, &itemCount);
  if (status != noErr) {
    CFRelease(pasteboard);
    error = clipboardStatusMessage("Could not inspect system clipboard", status);
    return false;
  }
  for (ItemCount index = 1; index <= itemCount; ++index) {
    PasteboardItemID item = nullptr;
    if (PasteboardGetItemIdentifier(pasteboard, static_cast<CFIndex>(index), &item) != noErr || !item) {
      continue;
    }
    if (copyClipboardFlavor(pasteboard, item, CFSTR("com.mcplugins.lookfilmlab.params"), text) ||
        copyClipboardFlavor(pasteboard, item, CFSTR("com.spektrafilm.ofx-params"), text) ||
        copyClipboardFlavor(pasteboard, item, CFSTR("public.utf8-plain-text"), text)) {
      found = true;
      break;
    }
  }
  CFRelease(pasteboard);
  return true;
}
#elif defined _WIN32
std::string clipboardStatusMessage(const char *prefix, DWORD status) {
  return std::string(prefix) + " (Win32 error " + std::to_string(static_cast<unsigned long>(status)) + ").";
}

UINT lookFilmLabClipboardFormat() {
  return RegisterClipboardFormatA("com.mcplugins.lookfilmlab.params");
}

UINT legacySpektraClipboardFormat() {
  return RegisterClipboardFormatA("com.spektrafilm.ofx-params");
}

bool writeClipboardBytes(UINT format, const std::string &text, std::string &error) {
  if (format == 0) {
    error = clipboardStatusMessage("Could not register LookFilmLab clipboard format", GetLastError());
    return false;
  }

  constexpr uint64_t headerSize = sizeof(uint64_t);
  const uint64_t textSize = static_cast<uint64_t>(text.size());
  if (textSize > static_cast<uint64_t>(std::numeric_limits<SIZE_T>::max() - headerSize)) {
    error = "LookFilmLab params are too large for the system clipboard.";
    return false;
  }

  const SIZE_T allocationSize = static_cast<SIZE_T>(headerSize + textSize);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, allocationSize);
  if (!memory) {
    error = clipboardStatusMessage("Could not allocate system clipboard memory", GetLastError());
    return false;
  }

  void *locked = GlobalLock(memory);
  if (!locked) {
    error = clipboardStatusMessage("Could not lock system clipboard memory", GetLastError());
    GlobalFree(memory);
    return false;
  }
  std::memcpy(locked, &textSize, sizeof(textSize));
  if (!text.empty()) {
    std::memcpy(static_cast<uint8_t *>(locked) + headerSize, text.data(), text.size());
  }
  GlobalUnlock(memory);

  if (!SetClipboardData(format, memory)) {
    error = clipboardStatusMessage("Could not write LookFilmLab params to system clipboard", GetLastError());
    GlobalFree(memory);
    return false;
  }
  return true;
}

void writeClipboardTextFallback(const std::string &text) {
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(text.size() + 1u));
  if (!memory) {
    return;
  }
  void *locked = GlobalLock(memory);
  if (!locked) {
    GlobalFree(memory);
    return;
  }
  std::memcpy(locked, text.data(), text.size());
  static_cast<char *>(locked)[text.size()] = '\0';
  GlobalUnlock(memory);
  if (!SetClipboardData(CF_TEXT, memory)) {
    GlobalFree(memory);
  }
}

bool writeTextToClipboard(const std::string &text, std::string &error) {
  error.clear();
  if (!OpenClipboard(nullptr)) {
    error = clipboardStatusMessage("Could not open system clipboard", GetLastError());
    return false;
  }

  if (!EmptyClipboard()) {
    error = clipboardStatusMessage("Could not clear system clipboard", GetLastError());
    CloseClipboard();
    return false;
  }

  const bool wroteBytes = writeClipboardBytes(lookFilmLabClipboardFormat(), text, error);
  if (wroteBytes) {
    writeClipboardTextFallback(text);
  }
  CloseClipboard();
  return wroteBytes;
}

bool readClipboardBytes(UINT format, std::string &text) {
  if (format == 0 || !IsClipboardFormatAvailable(format)) {
    return false;
  }
  HGLOBAL memory = GetClipboardData(format);
  if (!memory) {
    return false;
  }
  const SIZE_T allocationSize = GlobalSize(memory);
  if (allocationSize < sizeof(uint64_t)) {
    return false;
  }
  void *locked = GlobalLock(memory);
  if (!locked) {
    return false;
  }

  uint64_t textSize = 0;
  std::memcpy(&textSize, locked, sizeof(textSize));
  const SIZE_T payloadSize = allocationSize - sizeof(uint64_t);
  if (textSize > static_cast<uint64_t>(payloadSize)) {
    GlobalUnlock(memory);
    return false;
  }

  const auto *bytes = static_cast<const char *>(locked) + sizeof(uint64_t);
  text.assign(bytes, static_cast<size_t>(textSize));
  GlobalUnlock(memory);
  return true;
}

bool readClipboardTextFallback(std::string &text) {
  if (!IsClipboardFormatAvailable(CF_TEXT)) {
    return false;
  }
  HGLOBAL memory = GetClipboardData(CF_TEXT);
  if (!memory) {
    return false;
  }
  const char *locked = static_cast<const char *>(GlobalLock(memory));
  if (!locked) {
    return false;
  }
  text.assign(locked);
  GlobalUnlock(memory);
  return true;
}

bool readTextFromClipboard(std::string &text, bool &found, std::string &error) {
  text.clear();
  found = false;
  error.clear();
  if (!OpenClipboard(nullptr)) {
    error = clipboardStatusMessage("Could not open system clipboard", GetLastError());
    return false;
  }

  found =
    readClipboardBytes(lookFilmLabClipboardFormat(), text) ||
    readClipboardBytes(legacySpektraClipboardFormat(), text) ||
    readClipboardTextFallback(text);
  CloseClipboard();
  return true;
}
#else
bool writeTextToClipboard(const std::string &, std::string &error) {
  error = "Writing LookFilmLab params to the system clipboard is not implemented on this platform.";
  return false;
}

bool readTextFromClipboard(std::string &text, bool &found, std::string &error) {
  text.clear();
  found = false;
  error = "Reading LookFilmLab params from the system clipboard is not implemented on this platform.";
  return false;
}
#endif

void showMessage(OfxImageEffectHandle effect, const char *type, const char *id, const std::string &message) {
  if (gMessageHost) {
    gMessageHost->message(effect, type, id, "%s", message.c_str());
  }
}

bool openExternalUrl(const char *url) {
#if defined(__APPLE__)
  CFStringRef urlString = CFStringCreateWithCString(kCFAllocatorDefault, url, kCFStringEncodingUTF8);
  if (!urlString) {
    return false;
  }
  CFURLRef cfUrl = CFURLCreateWithString(kCFAllocatorDefault, urlString, nullptr);
  CFRelease(urlString);
  if (!cfUrl) {
    return false;
  }
  const OSStatus status = LSOpenCFURLRef(cfUrl, nullptr);
  CFRelease(cfUrl);
  return status == noErr;
#elif defined(_WIN32)
  HINSTANCE result = ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(result) > 32;
#else
  (void)url;
  return false;
#endif
}

bool openMacApplicationBundle(const char *path) {
#if defined(__APPLE__)
  CFURLRef appUrl = CFURLCreateFromFileSystemRepresentation(
    kCFAllocatorDefault,
    reinterpret_cast<const UInt8 *>(path),
    static_cast<CFIndex>(std::strlen(path)),
    true
  );
  if (!appUrl) {
    return false;
  }
  const OSStatus status = LSOpenCFURLRef(appUrl, nullptr);
  CFRelease(appUrl);
  return status == noErr;
#else
  (void)path;
  return false;
#endif
}

#if defined(_WIN32)
bool shellExecuteWindowsPath(const wchar_t *path, const wchar_t *parameters = nullptr) {
  HINSTANCE result = ShellExecuteW(nullptr, L"open", path, parameters, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<intptr_t>(result) > 32;
}

bool launchPowerShellHidden(const wchar_t *parameters) {
  std::wstring commandLine = L"powershell.exe ";
  commandLine += parameters;

  STARTUPINFOW startupInfo = {};
  startupInfo.cb = sizeof(startupInfo);
  startupInfo.dwFlags = STARTF_USESHOWWINDOW;
  startupInfo.wShowWindow = SW_HIDE;

  PROCESS_INFORMATION processInfo = {};
  const BOOL created = CreateProcessW(
    nullptr,
    &commandLine[0],
    nullptr,
    nullptr,
    FALSE,
    CREATE_NO_WINDOW,
    nullptr,
    nullptr,
    &startupInfo,
    &processInfo
  );
  if (!created) {
    return false;
  }
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return true;
}

bool launchWindowsExecutableIfExists(const wchar_t *pathWithEnvironment) {
  wchar_t expanded[MAX_PATH] = {};
  const DWORD expandedLength = ExpandEnvironmentStringsW(pathWithEnvironment, expanded, MAX_PATH);
  const wchar_t *path = (expandedLength > 0 && expandedLength < MAX_PATH) ? expanded : pathWithEnvironment;
  const DWORD attributes = GetFileAttributesW(path);
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
    return false;
  }
  return shellExecuteWindowsPath(path);
}

bool launchWindowsStoreMCNexus() {
  constexpr const wchar_t *kPowerShellArgs =
    LR"PS(-NoProfile -WindowStyle Hidden -Command "$app = Get-StartApps | Where-Object { $_.Name -eq 'MCNexus' } | Select-Object -First 1; if ($app) { Start-Process ('shell:AppsFolder\' + $app.AppID) } else { Start-Process 'https://apps.microsoft.com/detail/9n1qqt1xc825?hl=en-US&gl=US' }")PS";
  return launchPowerShellHidden(kPowerShellArgs);
}
#endif

bool openMCNexusApp() {
#if defined(__APPLE__)
  if (openMacApplicationBundle("/Applications/MCNexus.app")) {
    return true;
  }
  return openExternalUrl("https://github.com/ciqueira/MCNexus");
#elif defined(_WIN32)
  if (launchWindowsExecutableIfExists(L"%ProgramFiles%\\MCNexus\\MCNexus.exe") ||
      launchWindowsExecutableIfExists(L"%ProgramFiles(x86)%\\MCNexus\\MCNexus.exe") ||
      launchWindowsExecutableIfExists(L"%LocalAppData%\\Programs\\MCNexus\\MCNexus.exe")) {
    return true;
  }
  if (launchWindowsStoreMCNexus()) {
    return true;
  }
  return openExternalUrl("https://apps.microsoft.com/detail/9n1qqt1xc825?hl=en-US&gl=US");
#else
  return openExternalUrl("https://github.com/ciqueira/MCNexus");
#endif
}

bool getParamValueAtTime(OfxParamHandle handle, OfxTime time, const ParamDefault &entry, StoredParamValue &value) {
  if (!handle) {
    return false;
  }
  value.kind = entry.kind;
  switch (entry.kind) {
    case ParamValueKind::Int:
    case ParamValueKind::Bool: {
      int current = entry.intDefault;
      if (gParamHost->paramGetValueAtTime(handle, time, &current) != kOfxStatOK) {
        return false;
      }
      value.intValue[0] = current;
      return true;
    }
    case ParamValueKind::Double: {
      double current = entry.doubleDefault[0];
      if (gParamHost->paramGetValueAtTime(handle, time, &current) != kOfxStatOK) {
        return false;
      }
      value.doubleValue[0] = current;
      return true;
    }
    case ParamValueKind::Double2D: {
      double x = entry.doubleDefault[0];
      double y = entry.doubleDefault[1];
      if (gParamHost->paramGetValueAtTime(handle, time, &x, &y) != kOfxStatOK) {
        return false;
      }
      value.doubleValue[0] = x;
      value.doubleValue[1] = y;
      return true;
    }
    case ParamValueKind::Double3D: {
      double x = entry.doubleDefault[0];
      double y = entry.doubleDefault[1];
      double z = entry.doubleDefault[2];
      if (gParamHost->paramGetValueAtTime(handle, time, &x, &y, &z) != kOfxStatOK) {
        return false;
      }
      value.doubleValue[0] = x;
      value.doubleValue[1] = y;
      value.doubleValue[2] = z;
      return true;
    }
  }
  return false;
}

bool setParamValue(OfxParamHandle handle, const StoredParamValue &value) {
  if (!handle) {
    return false;
  }
  switch (value.kind) {
    case ParamValueKind::Int:
    case ParamValueKind::Bool:
      return gParamHost->paramSetValue(handle, value.intValue[0]) == kOfxStatOK;
    case ParamValueKind::Double:
      return gParamHost->paramSetValue(handle, value.doubleValue[0]) == kOfxStatOK;
    case ParamValueKind::Double2D:
      return gParamHost->paramSetValue(handle, value.doubleValue[0], value.doubleValue[1]) == kOfxStatOK;
    case ParamValueKind::Double3D:
      return gParamHost->paramSetValue(handle, value.doubleValue[0], value.doubleValue[1], value.doubleValue[2]) == kOfxStatOK;
  }
  return false;
}

void setParamParent(OfxPropertySetHandle props, const char *parent) {
  if (parent && parent[0] && shouldDefineGroup(parent)) {
    gPropHost->propSetString(props, kOfxParamPropParent, 0, parent);
  }
}

void setParamDescriptorHidden(OfxPropertySetHandle props, bool hidden) {
  gPropHost->propSetInt(props, kOfxParamPropSecret, 0, hidden ? 1 : 0);
  gPropHost->propSetInt(props, kOfxParamPropEnabled, 0, hidden ? 0 : 1);
}

void setParamHint(OfxPropertySetHandle props, const char *name) {
  const char *hint = spektrafilm::tooltipForParam(name);
  if (hint && hint[0]) {
    gPropHost->propSetString(props, kOfxParamPropHint, 0, hint);
  }
}

struct PageLayout {
  OfxPropertySetHandle props = nullptr;
  int childCount = 0;
};

PageLayout definePage(OfxParamSetHandle paramSet, const char *name, const char *label) {
  PageLayout page{};
  if (!isProProductionBuild()) {
    return page;
  }
  if (gParamHost->paramDefine(paramSet, kOfxParamTypePage, name, &page.props) != kOfxStatOK || !page.props) {
    page.props = nullptr;
    return page;
  }
  gPropHost->propSetString(page.props, kOfxPropLabel, 0, label);

  OfxPropertySetHandle paramSetProps = nullptr;
  if (gParamHost->paramSetGetPropertySet(paramSet, &paramSetProps) == kOfxStatOK && paramSetProps) {
    int pageCount = 0;
    gPropHost->propGetDimension(paramSetProps, kOfxPluginPropParamPageOrder, &pageCount);
    gPropHost->propSetString(paramSetProps, kOfxPluginPropParamPageOrder, pageCount, name);
  }
  return page;
}

void addPageChild(PageLayout &page, const char *name) {
  if (!page.props || !name || !name[0]) {
    return;
  }
  gPropHost->propSetString(page.props, kOfxParamPropPageChild, page.childCount, name);
  ++page.childCount;
}

void addProductionControlsPageChildren(PageLayout &page) {
  if (!isProProductionBuild()) {
    return;
  }

  if (isProductScan()) {
    addPageChild(page, "colorGroup");
    addPageChild(page, "scannedNegativeGroup");
    addPageChild(page, "productionStocksGroup");
    addPageChild(page, "productionLaboratoryGroup");
    addPageChild(page, "supportGroup");
    return;
  }

  addPageChild(page, "inputPrimariesColorSpace");
  addPageChild(page, "inputTransferColorSpace");
  addPageChild(page, "outputPrimariesColorSpace");
  addPageChild(page, "outputTransferColorSpace");

  addPageChild(page, "filmFormat");
  addPageChild(page, "filmExposureEv");
  addPageChild(page, "productionProfileNegative");
  addPageChild(page, "productionProfilePrint");
  addPageChild(page, "filmPushPullStops");
  addPageChild(page, "negativeBleachBypassAmount");
  if (isProductPhoto()) {
    addPageChild(page, "printPushPullStops");
    addPageChild(page, "printBleachBypassAmount");
  } else {
    addPageChild(page, "productionPrinterLightsEnabled");
    addPageChild(page, "productionPrinterLightsLinked");
    addPageChild(page, "creativePrinterLightR");
    addPageChild(page, "creativePrinterLightG");
    addPageChild(page, "creativePrinterLightB");
    addPageChild(page, "printBleachBypassAmount");
  }

  addPageChild(page, "supportAboutHelp");
  addPageChild(page, "supportOpenMCNexus");
}

const char *displayLabelForCurrentBuild(const char *name, const char *fallback) {
  if (!isProProductionBuild()) {
    return fallback;
  }
  if (std::strcmp(name, "colorGroup") == 0) {
    return "Technical Controls";
  }
  if (std::strcmp(name, "scannedNegativeGroup") == 0) {
    return "Scanned Negative";
  }
  if (std::strcmp(name, "inputPrimariesColorSpace") == 0) {
    return "Input Space";
  }
  if (std::strcmp(name, "inputTransferColorSpace") == 0) {
    return "Input Gamma";
  }
  if (std::strcmp(name, "outputPrimariesColorSpace") == 0) {
    return "Output Display";
  }
  if (std::strcmp(name, "outputTransferColorSpace") == 0) {
    return "Output Gamma";
  }
  if (std::strcmp(name, "filmExposureEv") == 0) {
    return "Exposure";
  }
  if (std::strcmp(name, "filmPushPullStops") == 0) {
    return isProductPhoto() ? "Negative Push/Pull" : "Push/Pull";
  }
  if (std::strcmp(name, "negativeBleachBypassAmount") == 0) {
    return "Bleach Bypass";
  }
  if (std::strcmp(name, "productionProfilePrint") == 0 && (isProductPhoto() || isProductScan())) {
    return "Profile Paper";
  }
  if (std::strcmp(name, "printPushPullStops") == 0 && (isProductPhoto() || isProductScan())) {
    return "Paper Push/Pull";
  }
  if (std::strcmp(name, "printBleachBypassAmount") == 0 && (isProductPhoto() || isProductScan())) {
    return "Paper Bleach Bypass";
  }
  return fallback;
}

void defineGroup(OfxParamSetHandle paramSet, const char *name, const char *label, bool openByDefault) {
  if (!shouldDefineGroup(name)) {
    return;
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeGroup, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, displayLabelForCurrentBuild(name, label));
  setParamHint(props, name);
  setParamDescriptorHidden(props, !groupVisibleInFlavor(name));
  gPropHost->propSetInt(props, kOfxParamPropGroupOpen, 0, openByDefault ? 1 : 0);
}

void defineChoice(OfxParamSetHandle paramSet, const char *name, const char *label, const char *const *options, int optionCount, int defaultValue, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  if (optionCount <= 0) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    defaultValue = stored.intValue[0];
  }
  defaultValue = std::clamp(defaultValue, 0, optionCount - 1);
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeChoice, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, displayLabelForCurrentBuild(name, label));
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  for (int i = 0; i < optionCount; ++i) {
    gPropHost->propSetString(props, kOfxParamPropChoiceOption, i, options[i]);
  }
  gPropHost->propSetInt(props, kOfxParamPropDefault, 0, defaultValue);
}

void defineDouble(OfxParamSetHandle paramSet, const char *name, const char *label, double defaultValue, double min, double max, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    defaultValue = stored.doubleValue[0];
  }
  defaultValue = std::clamp(defaultValue, min, max);
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeDouble, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, displayLabelForCurrentBuild(name, label));
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, defaultValue);
  gPropHost->propSetDouble(props, kOfxParamPropMin, 0, min);
  gPropHost->propSetDouble(props, kOfxParamPropMax, 0, max);
  gPropHost->propSetDouble(props, kOfxParamPropDisplayMin, 0, min);
  gPropHost->propSetDouble(props, kOfxParamPropDisplayMax, 0, max);
}

void defineInt(OfxParamSetHandle paramSet, const char *name, const char *label, int defaultValue, int min, int max, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    defaultValue = stored.intValue[0];
  }
  defaultValue = std::clamp(defaultValue, min, max);
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeInteger, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, displayLabelForCurrentBuild(name, label));
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetInt(props, kOfxParamPropDefault, 0, defaultValue);
  gPropHost->propSetInt(props, kOfxParamPropMin, 0, min);
  gPropHost->propSetInt(props, kOfxParamPropMax, 0, max);
  gPropHost->propSetInt(props, kOfxParamPropDisplayMin, 0, min);
  gPropHost->propSetInt(props, kOfxParamPropDisplayMax, 0, max);
}

void defineBool(OfxParamSetHandle paramSet, const char *name, const char *label, bool defaultValue, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    defaultValue = stored.intValue[0] != 0;
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeBoolean, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, displayLabelForCurrentBuild(name, label));
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetInt(props, kOfxParamPropDefault, 0, defaultValue ? 1 : 0);
}

void defineLabel(OfxParamSetHandle paramSet, const char *name, const char *descriptor, const char *value, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeString, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, descriptor);
  gPropHost->propSetString(props, kOfxPropShortLabel, 0, descriptor);
  gPropHost->propSetString(props, kOfxPropLongLabel, 0, descriptor);
  gPropHost->propSetString(props, kOfxParamPropDefault, 0, value);
  gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsLabel);
  gPropHost->propSetInt(props, kOfxParamPropEnabled, 0, 0);
  gPropHost->propSetInt(props, kOfxParamPropPersistant, 0, 0);
  gPropHost->propSetInt(props, kOfxParamPropEvaluateOnChange, 0, 0);
  setParamParent(props, parent);
}

void defineSingleLineString(OfxParamSetHandle paramSet, const char *name, const char *label, const char *defaultValue, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeString, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
  gPropHost->propSetString(props, kOfxParamPropDefault, 0, defaultValue);
  gPropHost->propSetInt(props, kOfxParamPropEvaluateOnChange, 0, 0);
}

void definePushButton(OfxParamSetHandle paramSet, const char *name, const char *label, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypePushButton, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetInt(props, kOfxParamPropPersistant, 0, 0);
  gPropHost->propSetInt(props, kOfxParamPropEvaluateOnChange, 0, 1);
}

void defineHiddenBool(OfxParamSetHandle paramSet, const char *name, bool value, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    value = stored.intValue[0] != 0;
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeBoolean, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, true);
  gPropHost->propSetInt(props, kOfxParamPropDefault, 0, value ? 1 : 0);
}

void defineRGB(OfxParamSetHandle paramSet, const char *name, const char *label, double r, double g, double b, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    r = stored.doubleValue[0];
    g = stored.doubleValue[1];
    b = stored.doubleValue[2];
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeRGB, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, r);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, g);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 2, b);
}

void defineDouble3D(OfxParamSetHandle paramSet, const char *name, const char *label, double x, double y, double z, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    x = stored.doubleValue[0];
    y = stored.doubleValue[1];
    z = stored.doubleValue[2];
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeDouble3D, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, x);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, y);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 2, z);
}

void defineDouble3DRange(
  OfxParamSetHandle paramSet,
  const char *name,
  const char *label,
  double x,
  double y,
  double z,
  double min,
  double max,
  const char *parent = nullptr
) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    x = stored.doubleValue[0];
    y = stored.doubleValue[1];
    z = stored.doubleValue[2];
  }
  x = std::clamp(x, min, max);
  y = std::clamp(y, min, max);
  z = std::clamp(z, min, max);
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeDouble3D, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, x);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, y);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 2, z);
  for (int i = 0; i < 3; ++i) {
    gPropHost->propSetDouble(props, kOfxParamPropMin, i, min);
    gPropHost->propSetDouble(props, kOfxParamPropMax, i, max);
    gPropHost->propSetDouble(props, kOfxParamPropDisplayMin, i, min);
    gPropHost->propSetDouble(props, kOfxParamPropDisplayMax, i, max);
  }
}

void defineDouble2D(OfxParamSetHandle paramSet, const char *name, const char *label, double x, double y, const char *parent = nullptr) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    x = stored.doubleValue[0];
    y = stored.doubleValue[1];
  }
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeDouble2D, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, x);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, y);
}

void defineDouble2DRange(
  OfxParamSetHandle paramSet,
  const char *name,
  const char *label,
  double x,
  double y,
  double min,
  double max,
  const char *parent = nullptr
) {
  if (!shouldDefineParam(name)) {
    return;
  }
  StoredParamValue stored{};
  if (storedValueForDefault(name, stored)) {
    x = stored.doubleValue[0];
    y = stored.doubleValue[1];
  }
  x = std::clamp(x, min, max);
  y = std::clamp(y, min, max);
  OfxPropertySetHandle props = nullptr;
  gParamHost->paramDefine(paramSet, kOfxParamTypeDouble2D, name, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, label);
  setParamHint(props, name);
  setParamParent(props, parent);
  setParamDescriptorHidden(props, parameterHiddenInFlavor(name));
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, x);
  gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, y);
  for (int i = 0; i < 2; ++i) {
    gPropHost->propSetDouble(props, kOfxParamPropMin, i, min);
    gPropHost->propSetDouble(props, kOfxParamPropMax, i, max);
    gPropHost->propSetDouble(props, kOfxParamPropDisplayMin, i, min);
    gPropHost->propSetDouble(props, kOfxParamPropDisplayMax, i, max);
  }
}

void cacheParam(OfxParamSetHandle paramSet, const char *name, OfxParamHandle &handle) {
  gParamHost->paramGetHandle(paramSet, name, &handle, nullptr);
}

OfxParamHandle paramHandleForName(OfxParamSetHandle paramSet, const char *name) {
  OfxParamHandle handle = nullptr;
  if (gParamHost->paramGetHandle(paramSet, name, &handle, nullptr) != kOfxStatOK) {
    return nullptr;
  }
  return handle;
}

std::string readStringProperty(OfxPropertySetHandle props, const char *name, int index = 0) {
  if (!props || !name || !gPropHost) {
    return {};
  }
  int dimension = 0;
  if (gPropHost->propGetDimension(props, name, &dimension) != kOfxStatOK || dimension <= index) {
    return {};
  }
  char *value = nullptr;
  if (gPropHost->propGetString(props, name, index, &value) != kOfxStatOK || !value || !value[0]) {
    return {};
  }
  return value;
}

std::string readStringListProperty(OfxPropertySetHandle props, const char *name) {
  if (!props || !name || !gPropHost) {
    return {};
  }
  int dimension = 0;
  if (gPropHost->propGetDimension(props, name, &dimension) != kOfxStatOK || dimension <= 0) {
    return {};
  }
  std::ostringstream stream;
  for (int i = 0; i < dimension; ++i) {
    const std::string value = readStringProperty(props, name, i);
    if (value.empty()) {
      continue;
    }
    if (stream.tellp() > 0) {
      stream << ", ";
    }
    stream << value;
  }
  return stream.str();
}

std::string readEffectStringProperty(OfxImageEffectHandle effect, const char *name) {
  OfxPropertySetHandle props = nullptr;
  if (!effect || gEffectHost->getPropertySet(effect, &props) != kOfxStatOK) {
    return {};
  }
  return readStringProperty(props, name);
}

std::string readClipStringProperty(OfxImageClipHandle clip, const char *name) {
  OfxPropertySetHandle props = nullptr;
  if (!clip || gEffectHost->clipGetPropertySet(clip, &props) != kOfxStatOK) {
    return {};
  }
  return readStringProperty(props, name);
}

std::string unavailableIfEmpty(const std::string &value) {
  return value.empty() ? "unavailable" : value;
}

std::string lowercaseAsciiForColourDiagnostics(std::string value) {
  for (char &c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool containsToken(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string knownColourspaceSummary(const std::string &colourspace) {
  const std::string value = lowercaseAsciiForColourDiagnostics(colourspace);
  if (value.empty()) {
    return {};
  }
  if (containsToken(value, "ofxcolourspace_source")) {
    return "Source clip";
  }
  if (containsToken(value, "acescg")) {
    return "ACEScg / Linear";
  }
  if (containsToken(value, "aces2065") || containsToken(value, "aces2065-1")) {
    return "ACES2065-1 / Linear";
  }
  if (containsToken(value, "arri") && (containsToken(value, "logc3") || containsToken(value, "log_c3"))) {
    return "ARRI Wide Gamut 3 / LogC3";
  }
  if (containsToken(value, "arri") && (containsToken(value, "logc4") || containsToken(value, "log_c4"))) {
    return "ARRI Wide Gamut 4 / LogC4";
  }
  if (containsToken(value, "canon") && containsToken(value, "log3")) {
    return "Canon Cinema Gamut / Canon Log 3";
  }
  if (containsToken(value, "canon") && containsToken(value, "log2")) {
    return "Canon Cinema Gamut / Canon Log 2";
  }
  if (containsToken(value, "davinci")) {
    return "DaVinci Wide Gamut / Intermediate";
  }
  if (containsToken(value, "rec2020") || containsToken(value, "bt2020")) {
    return "Rec.2020";
  }
  if (containsToken(value, "p3")) {
    return "P3";
  }
  if (containsToken(value, "adobergb") || containsToken(value, "adobe_rgb")) {
    return "Adobe RGB";
  }
  if (containsToken(value, "srgb")) {
    return "sRGB";
  }
  if (containsToken(value, "rec709") || containsToken(value, "bt709")) {
    return "Rec.709";
  }
  if (containsToken(value, "ofx_scene_linear")) {
    return "generic scene linear";
  }
  if (containsToken(value, "ofx_scene_log")) {
    return "generic scene log";
  }
  if (containsToken(value, "ofx_display_sdr")) {
    return "generic display SDR";
  }
  return {};
}

std::string summarizedColourspace(const std::string &colourspace) {
  const std::string summary = knownColourspaceSummary(colourspace);
  if (!summary.empty()) {
    return summary + " (" + colourspace + ")";
  }
  return unavailableIfEmpty(colourspace);
}

void setLabelValue(OfxParamHandle handle, const std::string &value) {
  if (handle && gParamHost) {
    gParamHost->paramSetValue(handle, value.c_str());
  }
}

void updateHostColourActionCounters(InstanceData *data) {
  if (!data) {
    return;
  }
  setLabelValue(data->hostClipPreferencesCallInfo, std::to_string(data->hostClipPreferencesCallCount));
  setLabelValue(data->hostOutputColourspaceCallInfo, std::to_string(data->hostOutputColourspaceCallCount));
}

std::string resolvedUserTimelineDiagnostic(
  const std::string &sourceColourspace,
  const std::string &outputColourspace,
  const std::string &displayColourspace
) {
  if (sourceColourspace.empty() && outputColourspace.empty() && displayColourspace.empty()) {
    return "fallback manual (host colourspace unavailable)";
  }
  std::ostringstream stream;
  stream << "Input " << summarizedColourspace(sourceColourspace);
  if (!outputColourspace.empty()) {
    stream << "; Output " << summarizedColourspace(outputColourspace);
  }
  if (!displayColourspace.empty()) {
    stream << "; Display " << summarizedColourspace(displayColourspace);
  }
  return stream.str();
}

void updateHostColourDiagnostics(OfxImageEffectHandle effect, InstanceData *data) {
  if (!data) {
    return;
  }
  const std::string hostStyle = readEffectStringProperty(effect, kOfxImageEffectPropColourManagementStyle);
  const std::string config = readEffectStringProperty(effect, kOfxImageEffectPropColourManagementConfig);
  const std::string ocioConfig = readEffectStringProperty(effect, kOfxImageEffectPropOCIOConfig);
  const std::string ocioDisplay = readEffectStringProperty(effect, kOfxImageEffectPropOCIODisplay);
  const std::string ocioView = readEffectStringProperty(effect, kOfxImageEffectPropOCIOView);
  const std::string sourceColourspace = readClipStringProperty(data->sourceClip, kOfxImageClipPropColourspace);
  const std::string outputColourspace = readClipStringProperty(data->outputClip, kOfxImageClipPropColourspace);
  std::string displayColourspace = readEffectStringProperty(effect, kOfxImageEffectPropDisplayColourspace);
  if (displayColourspace.empty() && (!ocioDisplay.empty() || !ocioView.empty())) {
    displayColourspace = "OCIO display=" + unavailableIfEmpty(ocioDisplay) + ", view=" + unavailableIfEmpty(ocioView);
  }

  setLabelValue(data->hostColourManagementInfo, unavailableIfEmpty(hostStyle));
  setLabelValue(data->colourManagementConfigInfo, unavailableIfEmpty(config));
  setLabelValue(data->ocioConfigInfo, unavailableIfEmpty(ocioConfig));
  setLabelValue(data->sourceColourspaceInfo, unavailableIfEmpty(sourceColourspace));
  setLabelValue(data->outputColourspaceInfo, unavailableIfEmpty(outputColourspace));
  setLabelValue(data->displayColourspaceInfo, unavailableIfEmpty(displayColourspace));
  setLabelValue(
    data->resolvedUserTimelineInfo,
    resolvedUserTimelineDiagnostic(sourceColourspace, outputColourspace, displayColourspace)
  );
}

void setPreferredColourspace(OfxPropertySetHandle outArgs, const char *clipName, int index, const char *colourspace) {
  if (!outArgs || !clipName || !colourspace || !gPropHost) {
    return;
  }
  const std::string propertyName = std::string(kOfxImageClipPropPreferredColourspaces) + "_" + clipName;
  gPropHost->propSetString(outArgs, propertyName.c_str(), index, colourspace);
}

OfxStatus getClipPreferences(OfxImageEffectHandle effect, OfxPropertySetHandle outArgs) {
  InstanceData *data = getInstanceData(effect);
  if (!data || !outArgs) {
    return kOfxStatReplyDefault;
  }
  ++data->hostClipPreferencesCallCount;
  updateHostColourActionCounters(data);

  switch (getIntValue(data->hostClipPreferenceMode, 0)) {
    case 1:
      setLabelValue(data->hostClipPreferenceRequestInfo, "ofx_scene_linear, ofx_scene_log, Raw");
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 0, kOfxColourspaceOfxSceneLinear);
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 1, kOfxColourspaceOfxSceneLog);
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 2, kOfxColourspaceRaw);
      return kOfxStatOK;
    case 2:
      setLabelValue(data->hostClipPreferenceRequestInfo, "ofx_scene_log, ofx_scene_linear, Raw");
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 0, kOfxColourspaceOfxSceneLog);
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 1, kOfxColourspaceOfxSceneLinear);
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 2, kOfxColourspaceRaw);
      return kOfxStatOK;
    case 3:
      setLabelValue(data->hostClipPreferenceRequestInfo, "rec1886_rec709_display, srgb_display, Raw");
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 0, kOfxColourspaceRec1886Rec709Display);
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 1, kOfxColourspaceSrgbDisplay);
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 2, kOfxColourspaceRaw);
      return kOfxStatOK;
    case 4:
      setLabelValue(data->hostClipPreferenceRequestInfo, "Raw");
      setPreferredColourspace(outArgs, kOfxImageEffectSimpleSourceClipName, 0, kOfxColourspaceRaw);
      return kOfxStatOK;
    default:
      setLabelValue(data->hostClipPreferenceRequestInfo, "ReplyDefault");
      return kOfxStatReplyDefault;
  }
}

OfxStatus getOutputColourspace(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  InstanceData *data = getInstanceData(effect);
  if (!data || !outArgs || !gPropHost) {
    return kOfxStatReplyDefault;
  }
  ++data->hostOutputColourspaceCallCount;
  updateHostColourActionCounters(data);
  setLabelValue(
    data->hostOutputPreferredRequestInfo,
    unavailableIfEmpty(readStringListProperty(inArgs, kOfxImageClipPropPreferredColourspaces))
  );

  const char *outputColourspace = nullptr;
  switch (getIntValue(data->hostOutputColourspaceMode, 0)) {
    case 1:
      outputColourspace = "OfxColourspace_" kOfxImageEffectSimpleSourceClipName;
      break;
    case 2:
      outputColourspace = kOfxColourspaceOfxSceneLinear;
      break;
    case 3:
      outputColourspace = kOfxColourspaceRaw;
      break;
    default:
      setLabelValue(data->hostOutputColourspaceReplyInfo, "ReplyDefault");
      return kOfxStatReplyDefault;
  }

  gPropHost->propSetString(outArgs, kOfxImageClipPropColourspace, 0, outputColourspace);
  setLabelValue(data->hostOutputColourspaceReplyInfo, outputColourspace);
  return kOfxStatOK;
}

void randomizeGrainSeedForNewInstance(InstanceData *data) {
  if (!data || !data->grainSeed) {
    return;
  }
  int current = 0;
  if (gParamHost->paramGetValue(data->grainSeed, &current) != kOfxStatOK) {
    return;
  }
  if (current == gDescriptorGrainSeedDefault || current == 0 || current == 1) {
    gParamHost->paramSetValue(data->grainSeed, randomGrainSeed());
  }
}

bool snapshotScopeAllowsParam(SnapshotScope scope, const char *name) {
  switch (scope) {
    case SnapshotScope::ProductionPublic:
      return productionPublicParam(name);
    case SnapshotScope::Calibration:
      return calibrationParam(name);
    case SnapshotScope::All:
    default:
      return true;
  }
}

bool applySnapshotToParamSet(
  OfxParamSetHandle paramSet,
  const DefaultsSnapshot &snapshot,
  SnapshotScope scope,
  bool includeGrainSeed
) {
  bool appliedAny = false;
  for (const ParamDefault &entry : kParamDefaults) {
    if (!shouldDefineParam(entry.name)) {
      continue;
    }
    if (!snapshotScopeAllowsParam(scope, entry.name)) {
      continue;
    }
    if (!includeGrainSeed && isGrainSeedParam(entry.name)) {
      continue;
    }
    const auto found = snapshot.find(entry.name);
    if (found == snapshot.end() || found->second.kind != entry.kind) {
      continue;
    }
    OfxParamHandle handle = paramHandleForName(paramSet, entry.name);
    if (handle && setParamValue(handle, found->second)) {
      appliedAny = true;
    }
  }
  return appliedAny;
}

bool applySnapshotToParamSet(OfxParamSetHandle paramSet, const DefaultsSnapshot &snapshot, bool includeGrainSeed = true) {
  return applySnapshotToParamSet(paramSet, snapshot, SnapshotScope::All, includeGrainSeed);
}

bool resetParamSetToFactory(OfxParamSetHandle paramSet) {
  bool resetAny = false;
  for (const ParamDefault &entry : kParamDefaults) {
    if (!shouldDefineParam(entry.name)) {
      continue;
    }
    OfxParamHandle handle = paramHandleForName(paramSet, entry.name);
    if (handle && setParamValue(handle, factoryStoredValue(entry))) {
      resetAny = true;
    }
  }
  return resetAny;
}

void captureParamSetSnapshot(
  OfxParamSetHandle paramSet,
  OfxTime time,
  DefaultsSnapshot &snapshot,
  SnapshotScope scope,
  bool includeGrainSeed
) {
  for (const ParamDefault &entry : kParamDefaults) {
    if (!shouldDefineParam(entry.name)) {
      continue;
    }
    if (!snapshotScopeAllowsParam(scope, entry.name)) {
      snapshot.erase(entry.name);
      continue;
    }
    if (!includeGrainSeed && isGrainSeedParam(entry.name)) {
      snapshot.erase(entry.name);
      continue;
    }
    OfxParamHandle handle = paramHandleForName(paramSet, entry.name);
    StoredParamValue value{};
    if (handle && getParamValueAtTime(handle, time, entry, value)) {
      snapshot[entry.name] = value;
    }
  }
}

void captureParamSetSnapshot(OfxParamSetHandle paramSet, OfxTime time, DefaultsSnapshot &snapshot, bool includeGrainSeed = true) {
  captureParamSetSnapshot(paramSet, time, snapshot, SnapshotScope::All, includeGrainSeed);
}

bool saveVisibleDefaults(OfxParamSetHandle paramSet, OfxTime time, std::string &error) {
  DefaultsSnapshot snapshot;
  bool found = false;
  if (!loadDefaultsFromFile(snapshot, found, error)) {
    return false;
  }
  captureParamSetSnapshot(
    paramSet,
    time,
    snapshot,
    isProProductionBuild() ? SnapshotScope::ProductionPublic : SnapshotScope::All,
    false
  );
  return saveDefaultsToFile(snapshot, error);
}

bool copyVisibleParams(OfxParamSetHandle paramSet, OfxTime time, std::string &error) {
  DefaultsSnapshot snapshot;
  captureParamSetSnapshot(
    paramSet,
    time,
    snapshot,
    isProProductionBuild() ? SnapshotScope::ProductionPublic : SnapshotScope::All,
    true
  );
  std::string text = encodeDefaultsSnapshot(snapshot);
  obfuscateDefaultsText(text);
  return writeTextToClipboard(text, error);
}

const char *pluginFlavorName() {
  switch (kPluginFlavor) {
    case PluginFlavor::Flow:
      return "spektrafilm flow";
    case PluginFlavor::Pro:
      return "LookFilmLab";
    case PluginFlavor::FilmDev:
      return "spektrafilm dev";
  }
  return "LookFilmLab";
}

const char *processName(spektrafilm::ProcessMode process) {
  switch (process) {
    case spektrafilm::ProcessMode::ScanNegative:
      return "Scan negative";
    case spektrafilm::ProcessMode::ProcessNegative:
      return "Process negative";
    case spektrafilm::ProcessMode::PrintSimulation:
    default:
      return "Print simulation";
  }
}

const char *outputRoleName(spektrafilm::OutputRole role) {
  switch (role) {
    case spektrafilm::OutputRole::DisplayHdr:
      return "Display Out HDR";
    case spektrafilm::OutputRole::Rcm:
      return "RCM/ACES (Beta)";
    case spektrafilm::OutputRole::DisplaySdr:
    default:
      return "Display Out SDR";
  }
}

const char *colorSpaceName(spektrafilm::ColorSpace colorSpace) {
  switch (colorSpace) {
    case spektrafilm::ColorSpace::ArriLogC4:
      return "ARRI Wide Gamut 4 / LogC4";
    case spektrafilm::ColorSpace::ArriLogC3Ei800:
      return "ARRI Wide Gamut 3 / LogC3";
    case spektrafilm::ColorSpace::BmdFilmWideGamutGen5:
      return "Blackmagic Wide Gamut / Film Gen 5";
    case spektrafilm::ColorSpace::DavinciIntermediateWideGamut:
      return "DaVinci Wide Gamut / Intermediate";
    case spektrafilm::ColorSpace::RedLog3G10RedWideGamutRgb:
      return "REDWideGamutRGB / Log3G10";
    case spektrafilm::ColorSpace::SonySLog3SGamut3:
      return "S-Gamut3 / S-Log3";
    case spektrafilm::ColorSpace::SonySLog3SGamut3Cine:
      return "S-Gamut3.Cine / S-Log3";
    case spektrafilm::ColorSpace::CanonLog2CinemaGamutD55:
      return "Cinema Gamut D55 / Canon Log 2";
    case spektrafilm::ColorSpace::CanonLog3CinemaGamutD55:
      return "Cinema Gamut D55 / Canon Log 3";
    case spektrafilm::ColorSpace::PanasonicVLogVGamut:
      return "V-Gamut / V-Log";
    case spektrafilm::ColorSpace::Aces2065_1:
      return "ACES2065-1 / Linear";
    case spektrafilm::ColorSpace::AcesCg:
      return "ACEScg / Linear";
    case spektrafilm::ColorSpace::AcesCct:
      return "ACEScg / ACEScct";
    case spektrafilm::ColorSpace::AcesCc:
      return "ACEScg / ACEScc";
    case spektrafilm::ColorSpace::LinearRec2020:
      return "Rec.2020 / Linear";
    case spektrafilm::ColorSpace::LinearRec709:
      return "Rec.709 / Linear";
    case spektrafilm::ColorSpace::LinearP3D65:
      return "P3-D65 / Linear";
    case spektrafilm::ColorSpace::Srgb:
      return "sRGB / sRGB";
    case spektrafilm::ColorSpace::DisplayP3:
      return "Display P3 / sRGB";
    case spektrafilm::ColorSpace::ProPhotoRgb:
      return "ProPhoto RGB / ProPhoto";
    case spektrafilm::ColorSpace::AdobeRgb1998:
      return "Adobe RGB (1998) / Gamma 2.199";
    case spektrafilm::ColorSpace::DciP3:
      return "DCI-P3 / Gamma 2.6";
    case spektrafilm::ColorSpace::P3D65Gamma22:
      return "P3-D65 / Gamma 2.2";
    case spektrafilm::ColorSpace::P3D65Gamma26:
      return "P3-D65 / Gamma 2.6";
    case spektrafilm::ColorSpace::Rec709Gamma22:
      return "Rec.709 / Gamma 2.2";
    case spektrafilm::ColorSpace::Rec709Gamma24:
      return "Rec.709 / Gamma 2.4";
  }
  return "Unknown";
}

std::string sanitizePathComponent(std::string value) {
  for (char &ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c)) {
      continue;
    }
    ch = '_';
  }
  while (value.find("__") != std::string::npos) {
    value.replace(value.find("__"), 2, "_");
  }
  while (!value.empty() && value.front() == '_') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '_') {
    value.pop_back();
  }
  return value.empty() ? "unknown" : value;
}

std::string getProfileName(const spektrafilm::ProfileCurveSet *profile, const char *fallback) {
  return profile && profile->name && profile->name[0] ? profile->name : fallback;
}

std::string currentDatePrefix() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::ostringstream out;
  out << std::put_time(&local, "%y%m%d");
  return out.str();
}

std::string currentTimestampSuffix() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::ostringstream out;
  out << std::put_time(&local, "%y%m%d_%H%M%S");
  return out.str();
}

std::string currentReadableTimestamp() {
  std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::ostringstream out;
  out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::string randomExportCode() {
  static std::mt19937 generator{std::random_device{}()};
  static std::uniform_int_distribution<int> distribution(0, 0xffffff);
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << distribution(generator);
  return out.str();
}

std::filesystem::path envPath(const char *name, const std::filesystem::path &fallback) {
  const char *value = std::getenv(name);
  return std::filesystem::path(value && value[0] ? value : fallback.string());
}

bool envFlagEnabledOrDefault(const char *name, bool defaultValue) {
  const char *value = std::getenv(name);
  if (!value || value[0] == '\0') {
    return defaultValue;
  }
  if (std::strcmp(value, "0") == 0 ||
      std::strcmp(value, "false") == 0 ||
      std::strcmp(value, "FALSE") == 0 ||
      std::strcmp(value, "no") == 0 ||
      std::strcmp(value, "NO") == 0 ||
      std::strcmp(value, "off") == 0 ||
      std::strcmp(value, "OFF") == 0) {
    return false;
  }
  return true;
}

bool resetRendererAfterEndSequence() {
  return envFlagEnabledOrDefault("SPEKTRAFILM_OFX_RESET_RENDERER_AFTER_END_SEQUENCE", false);
}

bool releaseTransientAfterEndSequence() {
#if defined(__APPLE__)
  constexpr bool defaultValue = true;
#else
  constexpr bool defaultValue = false;
#endif
  return envFlagEnabledOrDefault("SPEKTRAFILM_OFX_RELEASE_TRANSIENT_AFTER_END_SEQUENCE", defaultValue);
}

bool resetRendererOnPurgeCaches() {
  return envFlagEnabledOrDefault("SPEKTRAFILM_OFX_RESET_RENDERER_ON_PURGE", true);
}

bool resetRendererAfterLutExport() {
#if defined(__APPLE__)
  constexpr bool defaultValue = false;
#else
  constexpr bool defaultValue = true;
#endif
  return envFlagEnabledOrDefault("SPEKTRAFILM_OFX_RESET_RENDERER_AFTER_LUT_EXPORT", defaultValue);
}

std::filesystem::path homeFolder() {
#if defined _WIN32
  return envPath("USERPROFILE", ".");
#else
  return envPath("HOME", ".");
#endif
}

std::filesystem::path userLutFolder() {
#if defined _WIN32
  return homeFolder() / "Documents" / "MCLookFilmLab";
#elif defined __APPLE__
  return homeFolder() / "Movies" / "MCLookFilmLab";
#else
  return homeFolder() / "MCLookFilmLab";
#endif
}

std::filesystem::path userDocumentsFolder() {
#if defined _WIN32
  return homeFolder() / "Documents";
#else
  return homeFolder() / "Documents";
#endif
}

std::filesystem::path lutDestinationFolder(int destination) {
  const std::filesystem::path homePath = homeFolder();
  switch (destination) {
    case 1:
#if defined _WIN32
      return envPath("PROGRAMDATA", "C:\\ProgramData") /
        "Blackmagic Design" / "DaVinci Resolve" / "Support" / "LUT" / "MCLookFilmLab";
#elif defined __APPLE__
      return std::filesystem::path("/") / "Library" / "Application Support" /
        "Blackmagic Design" / "DaVinci Resolve" / "LUT" / "MCLookFilmLab";
#else
      return userLutFolder();
#endif
    case 2:
      return homePath / ".nuke" / "MCLookFilmLab";
    case 3:
#if defined _WIN32
      return envPath("PROGRAMFILES", "C:\\Program Files") /
        "Adobe" / "Common" / "LUTs" / "Creative" / "MCLookFilmLab";
#elif defined __APPLE__
      return std::filesystem::path("/") / "Library" / "Application Support" /
        "Adobe" / "Common" / "LUTs" / "Creative" / "MCLookFilmLab";
#else
      return userLutFolder();
#endif
    case 4:
#if defined _WIN32
      return userLutFolder();
#elif defined __APPLE__
      return homePath / "Library" / "Application Support" /
        "ProApps" / "Custom LUTs" / "MCLookFilmLab";
#else
      return userLutFolder();
#endif
    case 0:
    default:
      return userLutFolder();
  }
}

std::filesystem::path generatedLutExportPath(int destination, const std::string &identifier) {
  const std::filesystem::path folder = lutDestinationFolder(destination);
  const std::string cleanIdentifier = sanitizePathComponent(identifier.empty() ? "LookFilmLab" : identifier);
  const std::string date = currentDatePrefix();
  for (int attempt = 0; attempt < 64; ++attempt) {
    std::filesystem::path path = folder / (date + "_" + cleanIdentifier + "_" + randomExportCode() + ".cube");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      return path;
    }
  }
  return folder / (date + "_" + cleanIdentifier + "_" + randomExportCode() + ".cube");
}

std::string getStringValue(OfxParamHandle handle) {
  if (!handle) {
    return {};
  }
  char *value = nullptr;
  if (gParamHost->paramGetValue(handle, &value) != kOfxStatOK || !value) {
    return {};
  }
  return value;
}

constexpr const char *kPresetExtension = ".lookpreset";
constexpr const char *kLegacyPresetExtension = ".spkpreset";
constexpr const char *kPresetFormat = "lookfilmlab-preset-v1";
constexpr const char *kLegacyPresetFormat = "spektrafilm-preset-v1";

struct PresetEntry {
  std::string displayName;
  std::string created;
  std::filesystem::path path;
};

std::filesystem::path presetFolder() {
  return userDocumentsFolder() / "MCLookFilmLab" / "presets";
}

std::filesystem::path legacyPresetFolder() {
  return userDocumentsFolder() / "spektrafilm" / "presets";
}

std::vector<std::filesystem::path> presetFoldersForReading() {
  const std::filesystem::path current = presetFolder();
  const std::filesystem::path legacy = legacyPresetFolder();
  if (current.lexically_normal().string() == legacy.lexically_normal().string()) {
    return {current};
  }
  return {current, legacy};
}

std::string lowercaseAscii(std::string value) {
  for (char &ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

bool isPresetFilePath(const std::filesystem::path &path) {
  const std::string extension = lowercaseAscii(path.extension().string());
  return extension == kPresetExtension || extension == kLegacyPresetExtension;
}

std::string trimString(std::string value) {
  const auto isSpace = [](unsigned char ch) {
    return std::isspace(ch) != 0;
  };
  while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::string jsonEscape(const std::string &value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20u) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  return out.str();
}

bool appendUtf8Codepoint(uint32_t codepoint, std::string &out) {
  if (codepoint <= 0x7fu) {
    out.push_back(static_cast<char>(codepoint));
    return true;
  }
  if (codepoint <= 0x7ffu) {
    out.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    return true;
  }
  if (codepoint <= 0xffffu) {
    out.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    return true;
  }
  if (codepoint <= 0x10ffffu) {
    out.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    return true;
  }
  return false;
}

int hexNibble(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

bool jsonUnescapeString(const std::string &text, size_t &pos, std::string &value) {
  if (pos >= text.size() || text[pos] != '"') {
    return false;
  }
  ++pos;
  std::string decoded;
  while (pos < text.size()) {
    const char ch = text[pos++];
    if (ch == '"') {
      value = std::move(decoded);
      return true;
    }
    if (ch != '\\') {
      decoded.push_back(ch);
      continue;
    }
    if (pos >= text.size()) {
      return false;
    }
    const char escaped = text[pos++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        decoded.push_back(escaped);
        break;
      case 'b':
        decoded.push_back('\b');
        break;
      case 'f':
        decoded.push_back('\f');
        break;
      case 'n':
        decoded.push_back('\n');
        break;
      case 'r':
        decoded.push_back('\r');
        break;
      case 't':
        decoded.push_back('\t');
        break;
      case 'u': {
        if (pos + 4u > text.size()) {
          return false;
        }
        uint32_t codepoint = 0;
        for (int i = 0; i < 4; ++i) {
          const int nibble = hexNibble(text[pos++]);
          if (nibble < 0) {
            return false;
          }
          codepoint = (codepoint << 4u) | static_cast<uint32_t>(nibble);
        }
        if (!appendUtf8Codepoint(codepoint, decoded)) {
          return false;
        }
        break;
      }
      default:
        return false;
    }
  }
  return false;
}

bool extractJsonString(const std::string &text, const char *key, std::string &value) {
  const std::string quotedKey = std::string("\"") + key + "\"";
  size_t pos = text.find(quotedKey);
  if (pos == std::string::npos) {
    return false;
  }
  pos += quotedKey.size();
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos >= text.size() || text[pos] != ':') {
    return false;
  }
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  return jsonUnescapeString(text, pos, value);
}

std::string hexEncode(const std::string &value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(value.size() * 2u);
  for (unsigned char ch : value) {
    encoded.push_back(digits[ch >> 4u]);
    encoded.push_back(digits[ch & 0x0fu]);
  }
  return encoded;
}

bool hexDecode(const std::string &value, std::string &decoded) {
  if ((value.size() % 2u) != 0u) {
    return false;
  }
  std::string out;
  out.reserve(value.size() / 2u);
  for (size_t i = 0; i < value.size(); i += 2u) {
    const int high = hexNibble(value[i]);
    const int low = hexNibble(value[i + 1u]);
    if (high < 0 || low < 0) {
      return false;
    }
    out.push_back(static_cast<char>((high << 4) | low));
  }
  decoded = std::move(out);
  return true;
}

bool readTextFile(const std::filesystem::path &path, std::string &text, std::string &error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "Could not open LookFilmLab preset file for reading: " + path.string();
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (!input.good() && !input.eof()) {
    error = "Could not read LookFilmLab preset file: " + path.string();
    return false;
  }
  return true;
}

bool writePresetFile(
  const std::filesystem::path &path,
  const std::string &displayName,
  const DefaultsSnapshot &snapshot,
  std::string &error
) {
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "Could not create LookFilmLab preset folder: " + path.parent_path().string();
    return false;
  }

  std::string payload = encodeDefaultsSnapshot(snapshot);
  obfuscateDefaultsText(payload);

  std::ostringstream json;
  json << "{\n";
  json << "  \"format\": \"" << kPresetFormat << "\",\n";
  json << "  \"name\": \"" << jsonEscape(displayName) << "\",\n";
  json << "  \"created\": \"" << jsonEscape(currentReadableTimestamp()) << "\",\n";
  json << "  \"plugin\": \"" << jsonEscape(pluginFlavorName()) << "\",\n";
  json << "  \"version\": \"" << jsonEscape(SPEKTRAFILM_PRODUCT_VERSION_STRING) << "\",\n";
  json << "  \"payload_encoding\": \"obfuscated-snapshot-hex\",\n";
  json << "  \"payload_hex\": \"" << hexEncode(payload) << "\"\n";
  json << "}\n";

  const std::filesystem::path tempPath = path.string() + ".tmp";
  {
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "Could not open LookFilmLab preset file for writing: " + tempPath.string();
      return false;
    }
    const std::string text = json.str();
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
      error = "Could not write LookFilmLab preset file: " + tempPath.string();
      return false;
    }
  }

  std::filesystem::rename(tempPath, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
  }
  if (ec) {
    error = "Could not replace LookFilmLab preset file: " + path.string();
    return false;
  }
  return true;
}

bool readPresetSnapshot(
  const std::filesystem::path &path,
  DefaultsSnapshot &snapshot,
  std::string &displayName,
  std::string &error
) {
  error.clear();
  std::string text;
  if (!readTextFile(path, text, error)) {
    return false;
  }

  std::string format;
  if (!extractJsonString(text, "format", format) || (format != kPresetFormat && format != kLegacyPresetFormat)) {
    error = "LookFilmLab preset file is not a recognized preset: " + path.string();
    return false;
  }
  if (!extractJsonString(text, "name", displayName)) {
    displayName = path.stem().string();
  }
  std::string payloadEncoding;
  if (!extractJsonString(text, "payload_encoding", payloadEncoding) || payloadEncoding != "obfuscated-snapshot-hex") {
    error = "LookFilmLab preset file uses an unsupported payload encoding: " + path.string();
    return false;
  }
  std::string payloadHex;
  if (!extractJsonString(text, "payload_hex", payloadHex)) {
    error = "LookFilmLab preset file is missing its snapshot payload: " + path.string();
    return false;
  }
  std::string payload;
  if (!hexDecode(payloadHex, payload)) {
    error = "LookFilmLab preset file contains an invalid snapshot payload: " + path.string();
    return false;
  }
  obfuscateDefaultsText(payload);
  DefaultsSnapshot decoded;
  if (!decodeDefaultsSnapshot(payload, decoded)) {
    error = "LookFilmLab preset file payload is not a recognized params snapshot: " + path.string();
    return false;
  }
  snapshot = std::move(decoded);
  return true;
}

PresetEntry presetEntryForPath(const std::filesystem::path &path) {
  PresetEntry entry{};
  entry.path = path;
  entry.displayName = path.stem().string();
  std::string text;
  std::string ignoredError;
  if (readTextFile(path, text, ignoredError)) {
    std::string name;
    if (extractJsonString(text, "name", name) && !trimString(name).empty()) {
      entry.displayName = name;
    }
    extractJsonString(text, "created", entry.created);
  }
  return entry;
}

std::vector<PresetEntry> listPresetEntries() {
  std::vector<PresetEntry> entries;
  for (const std::filesystem::path &folder : presetFoldersForReading()) {
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) {
      continue;
    }
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
      std::error_code fileEc;
      if (!it->is_regular_file(fileEc) || fileEc) {
        continue;
      }
      if (!isPresetFilePath(it->path())) {
        continue;
      }
      entries.push_back(presetEntryForPath(it->path()));
    }
  }
  std::sort(entries.begin(), entries.end(), [](const PresetEntry &a, const PresetEntry &b) {
    const int nameCompare = a.displayName.compare(b.displayName);
    if (nameCompare != 0) {
      return nameCompare < 0;
    }
    const int createdCompare = a.created.compare(b.created);
    if (createdCompare != 0) {
      return createdCompare < 0;
    }
    return a.path.string() < b.path.string();
  });
  return entries;
}

std::vector<std::string> presetChoiceLabels(const std::vector<PresetEntry> &entries) {
  if (entries.empty()) {
    return {"No presets found"};
  }
  std::vector<std::string> labels;
  labels.reserve(entries.size());
  for (const PresetEntry &entry : entries) {
    labels.push_back(entry.displayName.empty() ? entry.path.stem().string() : entry.displayName);
  }
  return labels;
}

bool samePresetPath(const std::filesystem::path &a, const std::filesystem::path &b) {
  return a.lexically_normal().string() == b.lexically_normal().string();
}

int presetIndexForPath(const std::vector<PresetEntry> &entries, const std::filesystem::path &path) {
  for (size_t i = 0; i < entries.size(); ++i) {
    if (samePresetPath(entries[i].path, path)) {
      return static_cast<int>(i);
    }
  }
  return entries.empty() ? 0 : static_cast<int>(entries.size() - 1u);
}

void refreshPresetDropdown(InstanceData *data, const std::filesystem::path &selectedPath = {}) {
  if (!data || !data->presetSelection || !gParamHost || !gPropHost) {
    return;
  }
  const std::vector<PresetEntry> entries = listPresetEntries();
  const std::vector<std::string> labels = presetChoiceLabels(entries);
  std::vector<const char *> labelPointers;
  labelPointers.reserve(labels.size());
  for (const std::string &label : labels) {
    labelPointers.push_back(label.c_str());
  }

  OfxPropertySetHandle props = nullptr;
  if (gParamHost->paramGetPropertySet(data->presetSelection, &props) == kOfxStatOK && props) {
    gPropHost->propReset(props, kOfxParamPropChoiceOption);
    gPropHost->propSetStringN(props, kOfxParamPropChoiceOption, static_cast<int>(labelPointers.size()), labelPointers.data());
  }

  const int selected = !selectedPath.empty() ? presetIndexForPath(entries, selectedPath) : std::clamp(getIntValue(data->presetSelection, 0), 0, static_cast<int>(labels.size() - 1u));
  gParamHost->paramSetValue(data->presetSelection, selected);
}

std::string normalizedPresetName(const std::string &rawName) {
  const std::string trimmed = trimString(rawName);
  return trimmed.empty() ? "LookFilmLab_preset" : trimmed;
}

std::filesystem::path generatedPresetPath(const std::string &displayName) {
  const std::filesystem::path folder = presetFolder();
  const std::string cleanName = sanitizePathComponent(displayName.empty() ? "LookFilmLab_preset" : displayName);
  const std::string timestamp = currentTimestampSuffix();
  for (int attempt = 0; attempt < 64; ++attempt) {
    const std::string suffix = attempt == 0 ? timestamp : timestamp + "_" + randomExportCode();
    const std::filesystem::path path = folder / (cleanName + "_" + suffix + kPresetExtension);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      return path;
    }
  }
  return folder / (cleanName + "_" + timestamp + "_" + randomExportCode() + kPresetExtension);
}

std::filesystem::path calibrationFolder() {
  return userDocumentsFolder() / "MCLookFilmLab" / "calibration";
}

std::filesystem::path activeProductionCalibrationPath() {
  return calibrationFolder() / "active_production_calibration.lookfilmlab.json";
}

bool findActiveProductionCalibrationPath(std::filesystem::path &path) {
  std::error_code ec;
  const std::filesystem::path current = activeProductionCalibrationPath();
  if (std::filesystem::is_regular_file(current, ec)) {
    path = current;
    return true;
  }
  path = current;
  return false;
}

const char *calibrationScopeName(SnapshotScope scope) {
  switch (scope) {
    case SnapshotScope::ProductionPublic:
      return "productionPublic";
    case SnapshotScope::Calibration:
      return "calibration";
    case SnapshotScope::All:
    default:
      return "all";
  }
}

void appendJsonStoredParam(std::ostringstream &json, const StoredParamValue &value) {
  const int components = paramComponentCount(value.kind);
  json << "{\"kind\":" << static_cast<int>(value.kind) << ",\"value\":[";
  if (paramKindUsesDouble(value.kind)) {
    json << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (int i = 0; i < components; ++i) {
      if (i > 0) {
        json << ',';
      }
      json << value.doubleValue[i];
    }
  } else {
    for (int i = 0; i < components; ++i) {
      if (i > 0) {
        json << ',';
      }
      json << value.intValue[i];
    }
  }
  json << "]}";
}

void appendJsonSnapshotObject(std::ostringstream &json, const DefaultsSnapshot &snapshot) {
  json << "{";
  bool first = true;
  for (const auto &item : snapshot) {
    if (!first) {
      json << ",";
    }
    first = false;
    json << "\n      \"" << jsonEscape(item.first) << "\": ";
    appendJsonStoredParam(json, item.second);
  }
  if (!snapshot.empty()) {
    json << "\n    ";
  }
  json << "}";
}

std::string encodeCalibrationSnapshotJson(
  const CalibrationSnapshot &snapshot,
  const char *captureKind,
  int filmIndex,
  int paperIndex,
  bool activeMaster = false
) {
  std::ostringstream json;
  json << "{\n";
  json << "  \"format\": \"lookfilmlab-calibration-v1\",\n";
  json << "  \"role\": \"" << (activeMaster ? "active-work-master" : "calibration-history") << "\",\n";
  json << "  \"product\": \"LookFilmLab\",\n";
  json << "  \"purpose\": \"" << (activeMaster ? "Production build calibration master" : "Calibration history backup") << "\",\n";
  json << "  \"created\": \"" << jsonEscape(currentReadableTimestamp()) << "\",\n";
  json << "  \"updated\": \"" << jsonEscape(currentReadableTimestamp()) << "\",\n";
  json << "  \"plugin\": \"MCLookFilmLab\",\n";
  json << "  \"version\": \"" << jsonEscape(SPEKTRAFILM_PRODUCT_VERSION_STRING) << "\",\n";
  json << "  \"source_build_mode\": \"" << (isProCalibrationBuild() ? "CALIBRATION" : "PRODUCTION") << "\",\n";
  json << "  \"calibration_id\": \"" << jsonEscape(activeMaster ? "active_production_calibration" : randomExportCode()) << "\",\n";
  json << "  \"capture_kind\": \"" << jsonEscape(captureKind ? captureKind : "unknown") << "\",\n";
  json << "  \"film_index\": " << filmIndex << ",\n";
  json << "  \"paper_index\": " << paperIndex << ",\n";
  json << "  \"sections\": {\n";
  json << "    \"global\": ";
  appendJsonSnapshotObject(json, snapshot.global);
  json << ",\n    \"negative\": ";
  appendJsonSnapshotObject(json, snapshot.negative);
  json << ",\n    \"print\": ";
  appendJsonSnapshotObject(json, snapshot.print);
  json << ",\n    \"pairOverride\": ";
  appendJsonSnapshotObject(json, snapshot.pairOverride);
  json << "\n  }\n";
  json << "}\n";
  return json.str();
}

std::filesystem::path generatedCalibrationPath(const char *captureKind, int filmIndex, int paperIndex) {
  const std::filesystem::path folder = calibrationFolder();
  const std::string kind = sanitizePathComponent(captureKind && captureKind[0] ? captureKind : "calibration");
  const std::string base = kind + "_film" + std::to_string(filmIndex) + "_print" + std::to_string(paperIndex);
  const std::string timestamp = currentTimestampSuffix();
  for (int attempt = 0; attempt < 64; ++attempt) {
    const std::string suffix = attempt == 0 ? timestamp : timestamp + "_" + randomExportCode();
    const std::filesystem::path path = folder / (base + "_" + suffix + ".lookcalibration.json");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
      return path;
    }
  }
  return folder / (base + "_" + timestamp + "_" + randomExportCode() + ".lookcalibration.json");
}

bool writeTextFileAtomically(const std::filesystem::path &path, const std::string &text, std::string &error) {
  error.clear();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "Could not create folder: " + path.parent_path().string();
    return false;
  }
  const std::filesystem::path tempPath = path.string() + ".tmp";
  {
    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "Could not open file for writing: " + tempPath.string();
      return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
      error = "Could not write file: " + tempPath.string();
      return false;
    }
  }
  std::filesystem::rename(tempPath, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
  }
  if (ec) {
    error = "Could not replace file: " + path.string();
    return false;
  }
  return true;
}

void captureCalibrationSection(
  OfxParamSetHandle paramSet,
  OfxTime time,
  DefaultsSnapshot &section
) {
  section.clear();
  captureParamSetSnapshot(paramSet, time, section, SnapshotScope::Calibration, false);
}

void updateCalibrationSnapshotForCaptureKind(
  CalibrationSnapshot &snapshot,
  const DefaultsSnapshot &section,
  const std::string &kind
) {
  if (kind == "global") {
    snapshot.global = section;
  } else if (kind == "negative") {
    snapshot.negative = section;
  } else if (kind == "print") {
    snapshot.print = section;
  } else if (kind == "pair") {
    snapshot.pairOverride = section;
  } else {
    snapshot.global = section;
    snapshot.negative = section;
    snapshot.print = section;
    snapshot.pairOverride = section;
  }
}

bool applyCalibrationSnapshotToParamSet(OfxParamSetHandle paramSet, const CalibrationSnapshot &snapshot) {
  bool applied = false;
  applied = applySnapshotToParamSet(paramSet, snapshot.global, SnapshotScope::Calibration, false) || applied;
  applied = applySnapshotToParamSet(paramSet, snapshot.negative, SnapshotScope::Calibration, false) || applied;
  applied = applySnapshotToParamSet(paramSet, snapshot.print, SnapshotScope::Calibration, false) || applied;
  applied = applySnapshotToParamSet(paramSet, snapshot.pairOverride, SnapshotScope::Calibration, false) || applied;
  return applied;
}

bool loadActiveProductionCalibration(
  OfxParamSetHandle paramSet,
  std::filesystem::path &path,
  std::string &error
) {
  const bool found = findActiveProductionCalibrationPath(path);
  if (!found) {
    error = "No active LookFilmLab calibration master found. Expected: " + path.string();
    return false;
  }
  CalibrationSnapshot snapshot{};
  if (!readCalibrationSnapshotFile(path, snapshot, error)) {
    return false;
  }
  return applyCalibrationSnapshotToParamSet(paramSet, snapshot);
}

bool writeCalibrationCapture(
  OfxParamSetHandle paramSet,
  InstanceData *data,
  OfxTime time,
  const char *captureKind,
  std::filesystem::path &path,
  std::filesystem::path &activePath,
  std::string &error
) {
  if (!isProCalibrationBuild()) {
    error = "Calibration capture is only available in Pro Calibration builds.";
    return false;
  }

  const int filmIndex = getIntAtTime(data ? data->film : nullptr, time, static_cast<int>(spektrafilm::kSpektraDefaultFilmIndex));
  const int paperIndex = getIntAtTime(data ? data->paper : nullptr, time, static_cast<int>(spektrafilm::kSpektraDefaultPaperIndex));
  CalibrationSnapshot snapshot{};
  const std::string kind = captureKind && captureKind[0] ? captureKind : "export";
  DefaultsSnapshot currentSection;
  captureCalibrationSection(paramSet, time, currentSection);
  if (kind == "global") {
    snapshot.global = currentSection;
  } else if (kind == "negative") {
    snapshot.negative = currentSection;
  } else if (kind == "print") {
    snapshot.print = currentSection;
  } else if (kind == "pair") {
    snapshot.pairOverride = currentSection;
  } else {
    updateCalibrationSnapshotForCaptureKind(snapshot, currentSection, kind);
  }

  path = generatedCalibrationPath(kind.c_str(), filmIndex, paperIndex);
  if (!writeTextFileAtomically(path, encodeCalibrationSnapshotJson(snapshot, kind.c_str(), filmIndex, paperIndex), error)) {
    return false;
  }

  activePath = activeProductionCalibrationPath();
  CalibrationSnapshot activeSnapshot{};
  std::filesystem::path existingActivePath;
  if (findActiveProductionCalibrationPath(existingActivePath)) {
    std::string readError;
    if (!readCalibrationSnapshotFile(existingActivePath, activeSnapshot, readError)) {
      error = readError;
      return false;
    }
  }
  updateCalibrationSnapshotForCaptureKind(activeSnapshot, currentSection, kind);
  return writeTextFileAtomically(
    activePath,
    encodeCalibrationSnapshotJson(activeSnapshot, "active_master", filmIndex, paperIndex, true),
    error
  );
}

int currentLutSize(InstanceData *data) {
  const int selected = getIntValue(data ? data->lutSize : nullptr, 1);
  return selected == 0 ? 33 : 65;
}

int currentLutDestination(InstanceData *data) {
  const int selected = getIntValue(data ? data->lutDestination : nullptr, 0);
  return std::clamp(selected, 0, 4);
}

std::string joinLabels(const std::vector<std::string> &labels) {
  std::ostringstream out;
  for (size_t i = 0; i < labels.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << labels[i];
  }
  return out.str();
}

std::vector<std::string> lutDisabledEffectLabels(const spektrafilm::RenderParams &params) {
  std::vector<std::string> labels;
  if (params.autoExposure) {
    labels.push_back("auto exposure");
  }
  if (params.grainEnabled) {
    labels.push_back("grain");
  }
  if (params.halationEnabled && (params.scatterAmount > 0.0f || params.halationAmount > 0.0f)) {
    labels.push_back("halation");
  }
  if (params.cameraDiffusionEnabled && params.cameraDiffusionStrength > 0.0f) {
    labels.push_back("camera diffusion");
  }
  if (params.printDiffusionEnabled && params.printDiffusionStrength > 0.0f) {
    labels.push_back("print diffusion");
  }
  if (params.dirCouplersAmount > 0.0f && params.dirCouplersDiffusionUm > 0.0f) {
    labels.push_back("DIR diffusion");
  }
  if (std::abs(params.enlargerScale - 1.0f) > 1.0e-6f ||
      std::abs(params.enlargerOffsetXPercent) > 1.0e-6f ||
      std::abs(params.enlargerOffsetYPercent) > 1.0e-6f) {
    labels.push_back("film-plane transform");
  }
  if (params.scannerEnabled && params.scannerMtf50LpMm > 0.0f) {
    labels.push_back("scanner blur");
  }
  if (params.scannerEnabled && params.scannerUnsharpRadiusUm > 0.0f && params.scannerUnsharpAmount > 0.0f) {
    labels.push_back("scanner unsharp");
  }
  return labels;
}

spektrafilm::RenderParams lutSafeParams(spektrafilm::RenderParams params) {
  params.autoExposure = false;
  params.grainEnabled = false;
  params.grainModel = spektrafilm::GrainModel::Preview;
  params.grainAnimate = false;
  params.halationEnabled = false;
  params.scatterAmount = 0.0f;
  params.halationAmount = 0.0f;
  params.cameraDiffusionEnabled = false;
  params.printDiffusionEnabled = false;
  params.dirCouplersDiffusionUm = 0.0f;
  params.dirCouplersDiffusionTailUm = 0.0f;
  params.dirCouplersDiffusionTailWeight = 0.0f;
  params.enlargerScale = 1.0f;
  params.enlargerOffsetXPercent = 0.0f;
  params.enlargerOffsetYPercent = 0.0f;
  params.scannerMtf50LpMm = 0.0f;
  params.scannerUnsharpRadiusUm = 0.0f;
  params.scannerUnsharpAmount = 0.0f;
  return params;
}

bool writeCubeLut(
  const std::filesystem::path &path,
  int lutSize,
  const spektrafilm::RenderParams &sourceParams,
  const std::vector<float> &pixels,
  const std::vector<std::string> &disabledEffects,
  std::string &error
) {
  error.clear();
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "Could not create LUT export folder: " + path.parent_path().string();
      return false;
    }
  }

  const std::filesystem::path tempPath = path.string() + ".tmp";
  std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "Could not open LUT file for writing: " + tempPath.string();
    return false;
  }

  out << "TITLE \"" << pluginFlavorName() << ' ' << lutSize << "pt "
      << colorSpaceName(sourceParams.inputColorSpace) << " to "
      << colorSpaceName(sourceParams.outputColorSpace) << "\"\n";
  out << "# Generated by " << pluginFlavorName() << " OFX " << SPEKTRAFILM_PRODUCT_VERSION_STRING << "\n";
  out << "# Process: " << processName(sourceParams.process) << "\n";
  out << "# Output role: " << outputRoleName(sourceParams.outputRole) << "\n";
  out << "# Input color space: " << colorSpaceName(sourceParams.inputColorSpace) << "\n";
  out << "# Output color space: " << colorSpaceName(sourceParams.outputColorSpace) << "\n";
  out << "# Film: " << getProfileName(spektrafilm::filmProfileCurves(sourceParams.film), "Unknown Film") << "\n";
  out << "# Paper: " << getProfileName(spektrafilm::paperProfileCurves(sourceParams.paper), "Unknown Paper") << "\n";
  if (!disabledEffects.empty()) {
    out << "# Disabled for LUT export: " << joinLabels(disabledEffects) << "\n";
  }
  out << "LUT_3D_SIZE " << lutSize << "\n";
  out << "DOMAIN_MIN 0 0 0\n";
  out << "DOMAIN_MAX 1 1 1\n";
  out << std::setprecision(9);

  const size_t sampleCount = static_cast<size_t>(lutSize) * static_cast<size_t>(lutSize) * static_cast<size_t>(lutSize);
  if (pixels.size() < sampleCount * 4u) {
    error = "Rendered LUT buffer is smaller than expected.";
    return false;
  }
  for (size_t i = 0; i < sampleCount; ++i) {
    const float r = pixels[i * 4u];
    const float g = pixels[i * 4u + 1u];
    const float b = pixels[i * 4u + 2u];
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
      error = "Rendered LUT contains non-finite values.";
      return false;
    }
    out << r << ' ' << g << ' ' << b << '\n';
  }
  if (!out) {
    error = "Could not write LUT file: " + tempPath.string();
    return false;
  }
  out.close();

  std::filesystem::rename(tempPath, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tempPath, path, ec);
  }
  if (ec) {
    error = "Could not replace LUT file: " + path.string();
    return false;
  }
  return true;
}

bool exportCurrentLut(InstanceData *data, OfxTime time, std::filesystem::path &path, std::vector<std::string> &disabledEffects, std::string &error) {
  error.clear();
  disabledEffects.clear();
  if (!data) {
    error = "LUT export is not available because the renderer is not initialized.";
    return false;
  }

  const int lutSize = currentLutSize(data);
  spektrafilm::RenderParams params = readParams(data, time);
  if (params.outputRole != spektrafilm::OutputRole::DisplaySdr) {
    error = "LUT export is only available for Display Out SDR. Select an SDR output color space before exporting.";
    return false;
  }

  path = generatedLutExportPath(currentLutDestination(data), getStringValue(data->lutIdentifier));
  disabledEffects = lutDisabledEffectLabels(params);
  spektrafilm::RenderParams renderParams = lutSafeParams(params);

  const int width = lutSize * lutSize;
  const int height = lutSize;
  const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  std::vector<float> source(pixelCount * 4u, 1.0f);
  std::vector<float> destination(pixelCount * 4u, 0.0f);
  const float denominator = static_cast<float>(std::max(lutSize - 1, 1));
  for (int b = 0; b < lutSize; ++b) {
    for (int g = 0; g < lutSize; ++g) {
      for (int r = 0; r < lutSize; ++r) {
        const size_t index = static_cast<size_t>(b) * static_cast<size_t>(lutSize) * static_cast<size_t>(lutSize) +
          static_cast<size_t>(g) * static_cast<size_t>(lutSize) + static_cast<size_t>(r);
        source[index * 4u] = static_cast<float>(r) / denominator;
        source[index * 4u + 1u] = static_cast<float>(g) / denominator;
        source[index * 4u + 2u] = static_cast<float>(b) / denominator;
        source[index * 4u + 3u] = 1.0f;
      }
    }
  }

  spektrafilm::ImageView sourceView{};
  sourceView.data = source.data();
  sourceView.width = width;
  sourceView.height = height;
  sourceView.rowBytes = width * static_cast<int32_t>(4 * sizeof(float));
  sourceView.components = 4;
  sourceView.bytesPerComponent = 4;

  spektrafilm::MutableImageView destinationView{};
  destinationView.data = destination.data();
  destinationView.width = width;
  destinationView.height = height;
  destinationView.rowBytes = width * static_cast<int32_t>(4 * sizeof(float));
  destinationView.components = 4;
  destinationView.bytesPerComponent = 4;

  spektrafilm::RenderWindow window{0, 0, width, height};
  bool renderOk = false;
  {
    std::lock_guard<std::mutex> rendererLock(data->rendererMutex);
    spektrafilm::Renderer *renderer = ensureRenderer(data);
    if (!renderer) {
      error = "LUT export is not available because the renderer is not initialized.";
      return false;
    }
    renderOk = renderer->render(sourceView, destinationView, window, renderParams, time);
    if (!renderOk) {
      error = renderer->lastError().empty() ? "Could not render LUT samples." : renderer->lastError();
    }
  }
  if (!renderOk) {
    releaseInstanceRendererResources(data, resetRendererAfterLutExport());
    return false;
  }

  const bool wroteLut = writeCubeLut(path, lutSize, params, destination, disabledEffects, error);
  releaseInstanceRendererResources(data, resetRendererAfterLutExport());
  return wroteLut;
}

OfxStatus createInstance(OfxImageEffectHandle effect) {
  auto *data = new InstanceData();
  OfxPropertySetHandle effectProps = nullptr;
  OfxParamSetHandle paramSet = nullptr;
  gEffectHost->getPropertySet(effect, &effectProps);
  gEffectHost->getParamSet(effect, &paramSet);

  gEffectHost->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &data->sourceClip, nullptr);
  gEffectHost->clipGetHandle(effect, kOfxImageEffectOutputClipName, &data->outputClip, nullptr);

  cacheParam(paramSet, "filteringGroup", data->filteringGroup);
  cacheParam(paramSet, "enlargerGroup", data->enlargerGroup);
  cacheParam(paramSet, "filmGroup", data->filmGroup);
  cacheParam(paramSet, "printSourceGroup", data->printSourceGroup);
  cacheParam(paramSet, "printGroup", data->printGroup);
  cacheParam(paramSet, "filmScanGroup", data->filmScanGroup);
  cacheParam(paramSet, "scannedNegativeGroup", data->scannedNegativeGroup);
  cacheParam(paramSet, "couplerGroup", data->couplerGroup);
  cacheParam(paramSet, "calibrationGroup", data->calibrationGroup);
  cacheParam(paramSet, "calibrationBuildInfo", data->calibrationBuildInfo);
  cacheParam(paramSet, "activeCalibrationInfo", data->activeCalibrationInfo);
  cacheParam(paramSet, "hostColourManagementInfo", data->hostColourManagementInfo);
  cacheParam(paramSet, "colourManagementConfigInfo", data->colourManagementConfigInfo);
  cacheParam(paramSet, "ocioConfigInfo", data->ocioConfigInfo);
  cacheParam(paramSet, "sourceColourspaceInfo", data->sourceColourspaceInfo);
  cacheParam(paramSet, "outputColourspaceInfo", data->outputColourspaceInfo);
  cacheParam(paramSet, "displayColourspaceInfo", data->displayColourspaceInfo);
  cacheParam(paramSet, "resolvedUserTimelineInfo", data->resolvedUserTimelineInfo);
  cacheParam(paramSet, "hostClipPreferencesCallInfo", data->hostClipPreferencesCallInfo);
  cacheParam(paramSet, "hostOutputColourspaceCallInfo", data->hostOutputColourspaceCallInfo);
  cacheParam(paramSet, "hostClipPreferenceRequestInfo", data->hostClipPreferenceRequestInfo);
  cacheParam(paramSet, "hostOutputPreferredRequestInfo", data->hostOutputPreferredRequestInfo);
  cacheParam(paramSet, "hostOutputColourspaceReplyInfo", data->hostOutputColourspaceReplyInfo);
  cacheParam(paramSet, "hostClipPreferenceMode", data->hostClipPreferenceMode);
  cacheParam(paramSet, "hostOutputColourspaceMode", data->hostOutputColourspaceMode);
  cacheParam(paramSet, "refreshHostColourDiagnostics", data->refreshHostColourDiagnostics);
  cacheParam(paramSet, "saveGlobalCalibration", data->saveGlobalCalibration);
  cacheParam(paramSet, "saveNegativeCalibration", data->saveNegativeCalibration);
  cacheParam(paramSet, "savePrintCalibration", data->savePrintCalibration);
  cacheParam(paramSet, "savePairCalibration", data->savePairCalibration);
  cacheParam(paramSet, "exportProductionCalibration", data->exportProductionCalibration);
  cacheParam(paramSet, "loadActiveCalibration", data->loadActiveCalibration);
  cacheParam(paramSet, "productionCameraGroup", data->productionCameraGroup);
  cacheParam(paramSet, "productionStocksGroup", data->productionStocksGroup);
  cacheParam(paramSet, "productionLaboratoryGroup", data->productionLaboratoryGroup);
  cacheParam(paramSet, "grainGroup", data->grainGroup);
  cacheParam(paramSet, "grainSynthesisGroup", data->grainSynthesisGroup);
  cacheParam(paramSet, "halationGroup", data->halationGroup);
  cacheParam(paramSet, "process", data->process);
  cacheParam(paramSet, "scanNegativeInvert", data->scanNegativeInvert);
  cacheParam(paramSet, "rgbToRawMethod", data->rgbToRawMethod);
  cacheParam(paramSet, "inputColorSpace", data->inputColorSpace);
  cacheParam(paramSet, "inputPrimariesColorSpace", data->inputPrimariesColorSpace);
  cacheParam(paramSet, "inputTransferColorSpace", data->inputTransferColorSpace);
  cacheParam(paramSet, "rcmInputColorSpace", data->rcmInputColorSpace);
  cacheParam(paramSet, "outputRole", data->outputRole);
  cacheParam(paramSet, "sdrOutputColorSpace", data->sdrOutputColorSpace);
  cacheParam(paramSet, "outputPrimariesColorSpace", data->outputPrimariesColorSpace);
  cacheParam(paramSet, "outputTransferColorSpace", data->outputTransferColorSpace);
  cacheParam(paramSet, "sceneOutputColorSpace", data->sceneOutputColorSpace);
  cacheParam(paramSet, "hdrPreset", data->hdrPreset);
  cacheParam(paramSet, "hdrTransfer", data->hdrTransfer);
  cacheParam(paramSet, "hdrReferenceWhiteNits", data->hdrReferenceWhiteNits);
  cacheParam(paramSet, "hdrPeakNits", data->hdrPeakNits);
  cacheParam(paramSet, "hdrExposureEv", data->hdrExposureEv);
  cacheParam(paramSet, "hdrToneMapping", data->hdrToneMapping);
  cacheParam(paramSet, "colorAdaptation", data->colorAdaptation);
  cacheParam(paramSet, "colorAdaptationInputCompression", data->colorAdaptationInputCompression);
  cacheParam(paramSet, "colorAdaptationCurveSmoothing", data->colorAdaptationCurveSmoothing);
  cacheParam(paramSet, "colorAdaptationOutputLightnessCompression", data->colorAdaptationOutputLightnessCompression);
  cacheParam(paramSet, "colorAdaptationOutputChromaCompression", data->colorAdaptationOutputChromaCompression);
  cacheParam(paramSet, "cameraUvFilterEnabled", data->cameraUvFilterEnabled);
  cacheParam(paramSet, "cameraUvCutNm", data->cameraUvCutNm);
  cacheParam(paramSet, "cameraIrFilterEnabled", data->cameraIrFilterEnabled);
  cacheParam(paramSet, "cameraIrCutNm", data->cameraIrCutNm);
  cacheParam(paramSet, "productionProfileNegative", data->productionProfileNegative);
  cacheParam(paramSet, "productionProfilePrint", data->productionProfilePrint);
  cacheParam(paramSet, "film", data->film);
  cacheParam(paramSet, "paper", data->paper);
  cacheParam(paramSet, "printTiming", data->printTiming);
  cacheParam(paramSet, "printSource", data->printSource);
  cacheParam(paramSet, "scanInputEncoding", data->scanInputEncoding);
  cacheParam(paramSet, "scanInputColorSpace", data->scanInputColorSpace);
  cacheParam(paramSet, "scanWorkingColorSpace", data->scanWorkingColorSpace);
  cacheParam(paramSet, "scanDensityBasis", data->scanDensityBasis);
  cacheParam(paramSet, "scanFilmBaseRgb", data->scanFilmBaseRgb);
  cacheParam(paramSet, "scanFilmBaseColorRgb", data->scanFilmBaseColorRgb);
  cacheParam(paramSet, "scanFilmBaseTemp", data->scanFilmBaseTemp);
  cacheParam(paramSet, "scanFilmBaseTint", data->scanFilmBaseTint);
  cacheParam(paramSet, "scanBlackFlareRgb", data->scanBlackFlareRgb);
  cacheParam(paramSet, "scanExposureEv", data->scanExposureEv);
  cacheParam(paramSet, "scanDensityContrast", data->scanDensityContrast);
  cacheParam(paramSet, "scanDensityScaleRgb", data->scanDensityScaleRgb);
  cacheParam(paramSet, "scanDensityScaleR", data->scanDensityScaleR);
  cacheParam(paramSet, "scanDensityScaleG", data->scanDensityScaleG);
  cacheParam(paramSet, "scanDensityScaleB", data->scanDensityScaleB);
  cacheParam(paramSet, "scanDensityOffsetRgb", data->scanDensityOffsetRgb);
  cacheParam(paramSet, "filmExposureEv", data->filmExposureEv);
  cacheParam(paramSet, "autoExposure", data->autoExposure);
  cacheParam(paramSet, "autoExposureMethod", data->autoExposureMethod);
  cacheParam(paramSet, "printExposureEv", data->printExposureEv);
  cacheParam(paramSet, "filmPushPullMode", data->filmPushPullMode);
  cacheParam(paramSet, "filmPushPullStops", data->filmPushPullStops);
  cacheParam(paramSet, "printPushPullStops", data->printPushPullStops);
  cacheParam(paramSet, "negativeBleachBypassAmount", data->negativeBleachBypassAmount);
  cacheParam(paramSet, "negativeLeucoCyanCoupling", data->negativeLeucoCyanCoupling);
  cacheParam(paramSet, "printBleachBypassAmount", data->printBleachBypassAmount);
  cacheParam(paramSet, "filmGamma", data->filmGamma);
  cacheParam(paramSet, "printGamma", data->printGamma);
  cacheParam(paramSet, "printShadowShape", data->printShadowShape);
  cacheParam(paramSet, "printHighlightShape", data->printHighlightShape);
  cacheParam(paramSet, "filterC", data->filterC);
  cacheParam(paramSet, "filterMShift", data->filterMShift);
  cacheParam(paramSet, "filterYShift", data->filterYShift);
  cacheParam(paramSet, "enlargerScale", data->enlargerScale);
  cacheParam(paramSet, "enlargerOffsetXPercent", data->enlargerOffsetXPercent);
  cacheParam(paramSet, "enlargerOffsetYPercent", data->enlargerOffsetYPercent);
  cacheParam(paramSet, "preflashExposure", data->preflashExposure);
  cacheParam(paramSet, "preflashMFilterShift", data->preflashMFilterShift);
  cacheParam(paramSet, "preflashYFilterShift", data->preflashYFilterShift);
  cacheParam(paramSet, "productionPrinterLightsEnabled", data->productionPrinterLightsEnabled);
  cacheParam(paramSet, "productionPrinterLightsLinked", data->productionPrinterLightsLinked);
  cacheParam(paramSet, "creativePrinterLightR", data->creativePrinterLightR);
  cacheParam(paramSet, "creativePrinterLightG", data->creativePrinterLightG);
  cacheParam(paramSet, "creativePrinterLightB", data->creativePrinterLightB);
  cacheParam(paramSet, "printerLightR", data->printerLightR);
  cacheParam(paramSet, "printerLightG", data->printerLightG);
  cacheParam(paramSet, "printerLightB", data->printerLightB);
  cacheParam(paramSet, "printerLightsGang", data->printerLightsGang);
  cacheParam(paramSet, "printerLightsGroup", data->printerLightsGroup);
  cacheParam(paramSet, "printerLightCalibration", data->printerLightCalibration);
  cacheParam(paramSet, "dirAmount", data->dirAmount);
  cacheParam(paramSet, "dirDiffusionUm", data->dirDiffusionUm);
  cacheParam(paramSet, "dirDiffusionTailUm", data->dirDiffusionTailUm);
  cacheParam(paramSet, "dirDiffusionTailWeight", data->dirDiffusionTailWeight);
  cacheParam(paramSet, "dirInhibitionSameLayer", data->dirInhibitionSameLayer);
  cacheParam(paramSet, "dirInhibitionInterlayer", data->dirInhibitionInterlayer);
  cacheParam(paramSet, "dirGammaSameLayerRgb", data->dirGammaSameLayerRgb);
  cacheParam(paramSet, "dirGammaRToGb", data->dirGammaRToGb);
  cacheParam(paramSet, "dirGammaGToRb", data->dirGammaGToRb);
  cacheParam(paramSet, "dirGammaBToRg", data->dirGammaBToRg);
  cacheParam(paramSet, "dirCalibrateToStock", data->dirCalibrateToStock);
  cacheParam(paramSet, "dirUsesStockCalibration", data->dirUsesStockCalibration);
  cacheParam(paramSet, "grainEnabled", data->grainEnabled);
  cacheParam(paramSet, "grainModel", data->grainModel);
  cacheParam(paramSet, "filmFormat", data->filmFormat);
  cacheParam(paramSet, "grainAmount", data->grainAmount);
  cacheParam(paramSet, "grainSaturation", data->grainSaturation);
  cacheParam(paramSet, "grainSublayersEnabled", data->grainSublayersEnabled);
  cacheParam(paramSet, "grainSubLayerCount", data->grainSubLayerCount);
  cacheParam(paramSet, "grainParticleAreaUm2", data->grainParticleAreaUm2);
  cacheParam(paramSet, "grainParticleScale", data->grainParticleScale);
  cacheParam(paramSet, "grainParticleScaleLayers", data->grainParticleScaleLayers);
  cacheParam(paramSet, "grainDensityMin", data->grainDensityMin);
  cacheParam(paramSet, "grainUniformity", data->grainUniformity);
  cacheParam(paramSet, "grainFinalBlurUm", data->grainFinalBlurUm);
  cacheParam(paramSet, "grainBlurDyeCloudsUm", data->grainBlurDyeCloudsUm);
  cacheParam(paramSet, "grainMicroStructure", data->grainMicroStructure);
  cacheParam(paramSet, "grainSeed", data->grainSeed);
  cacheParam(paramSet, "grainAnimate", data->grainAnimate);
  cacheParam(paramSet, "grainSynthesisSize", data->grainSynthesisSize);
  cacheParam(paramSet, "grainSynthesisAmount", data->grainSynthesisAmount);
  cacheParam(paramSet, "grainSynthesisSharpness", data->grainSynthesisSharpness);
  cacheParam(paramSet, "grainSynthesisQuality", data->grainSynthesisQuality);
  cacheParam(paramSet, "grainSynthesisSamples", data->grainSynthesisSamples);
  cacheParam(paramSet, "grainSynthesisMeanRadiusUm", data->grainSynthesisMeanRadiusUm);
  cacheParam(paramSet, "grainSynthesisRadiusStdDevRatio", data->grainSynthesisRadiusStdDevRatio);
  cacheParam(paramSet, "grainSynthesisObservationSigmaUm", data->grainSynthesisObservationSigmaUm);
  cacheParam(paramSet, "grainSynthesisCellSizeRatio", data->grainSynthesisCellSizeRatio);
  cacheParam(paramSet, "grainSynthesisMaxRadiusQuantile", data->grainSynthesisMaxRadiusQuantile);
  cacheParam(paramSet, "grainSynthesisCoverageEpsilon", data->grainSynthesisCoverageEpsilon);
  cacheParam(paramSet, "grainSynthesisMaxGrainsPerCell", data->grainSynthesisMaxGrainsPerCell);
  cacheParam(paramSet, "grainSynthesisRadiusScale", data->grainSynthesisRadiusScale);
  cacheParam(paramSet, "grainSynthesisLayerScale", data->grainSynthesisLayerScale);
  cacheParam(paramSet, "grainSynthesisLayered", data->grainSynthesisLayered);
  cacheParam(paramSet, "halationEnabled", data->halationEnabled);
  cacheParam(paramSet, "scatterAmount", data->scatterAmount);
  cacheParam(paramSet, "scatterScale", data->scatterScale);
  cacheParam(paramSet, "halationAmount", data->halationAmount);
  cacheParam(paramSet, "halationScale", data->halationScale);
  cacheParam(paramSet, "halationStrength", data->halationStrength);
  cacheParam(paramSet, "halationBoostEv", data->halationBoostEv);
  cacheParam(paramSet, "halationBoostRange", data->halationBoostRange);
  cacheParam(paramSet, "halationProtectEv", data->halationProtectEv);
  cacheParam(paramSet, "cameraDiffusionEnabled", data->cameraDiffusionEnabled);
  cacheParam(paramSet, "cameraDiffusionFamily", data->cameraDiffusionFamily);
  cacheParam(paramSet, "cameraDiffusionStrength", data->cameraDiffusionStrength);
  cacheParam(paramSet, "cameraDiffusionSpatialScale", data->cameraDiffusionSpatialScale);
  cacheParam(paramSet, "cameraDiffusionHaloWarmth", data->cameraDiffusionHaloWarmth);
  cacheParam(paramSet, "cameraDiffusionCoreIntensity", data->cameraDiffusionCoreIntensity);
  cacheParam(paramSet, "cameraDiffusionCoreSize", data->cameraDiffusionCoreSize);
  cacheParam(paramSet, "cameraDiffusionHaloIntensity", data->cameraDiffusionHaloIntensity);
  cacheParam(paramSet, "cameraDiffusionHaloSize", data->cameraDiffusionHaloSize);
  cacheParam(paramSet, "cameraDiffusionBloomIntensity", data->cameraDiffusionBloomIntensity);
  cacheParam(paramSet, "cameraDiffusionBloomSize", data->cameraDiffusionBloomSize);
  cacheParam(paramSet, "printDiffusionEnabled", data->printDiffusionEnabled);
  cacheParam(paramSet, "printDiffusionFamily", data->printDiffusionFamily);
  cacheParam(paramSet, "printDiffusionStrength", data->printDiffusionStrength);
  cacheParam(paramSet, "printDiffusionSpatialScale", data->printDiffusionSpatialScale);
  cacheParam(paramSet, "printDiffusionHaloWarmth", data->printDiffusionHaloWarmth);
  cacheParam(paramSet, "printDiffusionCoreIntensity", data->printDiffusionCoreIntensity);
  cacheParam(paramSet, "printDiffusionCoreSize", data->printDiffusionCoreSize);
  cacheParam(paramSet, "printDiffusionHaloIntensity", data->printDiffusionHaloIntensity);
  cacheParam(paramSet, "printDiffusionHaloSize", data->printDiffusionHaloSize);
  cacheParam(paramSet, "printDiffusionBloomIntensity", data->printDiffusionBloomIntensity);
  cacheParam(paramSet, "printDiffusionBloomSize", data->printDiffusionBloomSize);
  cacheParam(paramSet, "scannerGroup", data->scannerGroup);
  cacheParam(paramSet, "scannerEnabled", data->scannerEnabled);
  cacheParam(paramSet, "scannerWhiteCorrection", data->scannerWhiteCorrection);
  cacheParam(paramSet, "scannerBlackCorrection", data->scannerBlackCorrection);
  cacheParam(paramSet, "scannerWhiteLevel", data->scannerWhiteLevel);
  cacheParam(paramSet, "scannerBlackLevel", data->scannerBlackLevel);
  cacheParam(paramSet, "glarePercent", data->glarePercent);
  cacheParam(paramSet, "glareRoughness", data->glareRoughness);
  cacheParam(paramSet, "glareBlur", data->glareBlur);
  cacheParam(paramSet, "scannerMtf50LpMm", data->scannerMtf50LpMm);
  cacheParam(paramSet, "scannerUnsharpRadiusUm", data->scannerUnsharpRadiusUm);
  cacheParam(paramSet, "scannerUnsharpAmount", data->scannerUnsharpAmount);
  cacheParam(paramSet, "gpuRenderTiling", data->gpuRenderTiling);
  cacheParam(paramSet, "lutSize", data->lutSize);
  cacheParam(paramSet, "lutDestination", data->lutDestination);
  cacheParam(paramSet, "lutIdentifier", data->lutIdentifier);
  cacheParam(paramSet, "exportLut", data->exportLut);
  cacheParam(paramSet, "presetName", data->presetName);
  cacheParam(paramSet, "presetSelection", data->presetSelection);
  cacheParam(paramSet, "savePreset", data->savePreset);
  cacheParam(paramSet, "loadPreset", data->loadPreset);
  refreshPresetDropdown(data);

  DefaultsSnapshot savedDefaults;
  bool defaultsFound = false;
  std::string defaultsError;
  if (loadDefaultsFromFile(savedDefaults, defaultsFound, defaultsError) && defaultsFound) {
    applySnapshotToParamSet(
      paramSet,
      savedDefaults,
      isProProductionBuild() ? SnapshotScope::ProductionPublic : SnapshotScope::All,
      false
    );
  }
  if (isProCalibrationBuild()) {
    std::filesystem::path activePath;
    if (findActiveProductionCalibrationPath(activePath)) {
      CalibrationSnapshot activeSnapshot{};
      std::string activeError;
      if (readCalibrationSnapshotFile(activePath, activeSnapshot, activeError)) {
        applyCalibrationSnapshotToParamSet(paramSet, activeSnapshot);
      }
    }
  }
  randomizeGrainSeedForNewInstance(data);
  if (dirUsesStockCalibration(data)) {
    applyDirStockCalibration(data, false);
  }

  rememberCurrentPrinterLights(data);
  rememberCurrentCreativePrinterLights(data);
  updateHostColourActionCounters(data);
  updateHostColourDiagnostics(effect, data);
  syncConditionalParamVisibility(data);
  gPropHost->propSetPointer(effectProps, kOfxPropInstanceData, 0, data);
  return kOfxStatOK;
}

OfxStatus destroyInstance(OfxImageEffectHandle effect) {
  InstanceData *data = getInstanceData(effect);
  delete data;
  return kOfxStatOK;
}

void releaseInstanceRendererResources(InstanceData *data, bool resetRenderer) {
  if (!data) {
    return;
  }
  std::lock_guard<std::mutex> rendererLock(data->rendererMutex);
  if (data->renderer) {
    if (resetRenderer) {
      data->renderer.reset();
    } else {
      data->renderer->releaseTransientResources();
    }
  }
}

OfxStatus releaseInactiveInstanceResources(OfxImageEffectHandle effect, bool resetRenderer) {
  if (!effect) {
    return kOfxStatOK;
  }
  releaseInstanceRendererResources(getInstanceData(effect), resetRenderer);
  return kOfxStatOK;
}

OfxStatus instanceChanged(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) {
  InstanceData *data = getInstanceData(effect);
  if (!data) {
    return kOfxStatReplyDefault;
  }
  char *changedName = nullptr;
  char *changeReason = nullptr;
  if (inArgs) {
    gPropHost->propGetString(inArgs, kOfxPropName, 0, &changedName);
    gPropHost->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);
  }
  if (changeReason && std::strcmp(changeReason, kOfxChangePluginEdited) == 0) {
    return kOfxStatReplyDefault;
  }

  const bool copyParamsChanged = changedName && std::strcmp(changedName, "copyParams") == 0;
  const bool pasteParamsChanged = changedName && std::strcmp(changedName, "pasteParams") == 0;
  const bool saveDefaultsChanged = changedName && std::strcmp(changedName, "saveDefaults") == 0;
  const bool resetDefaultsChanged = changedName && std::strcmp(changedName, "resetDefaults") == 0;
  const bool exportLutChanged = changedName && std::strcmp(changedName, "exportLut") == 0;
  const bool savePresetChanged = changedName && std::strcmp(changedName, "savePreset") == 0;
  const bool loadPresetChanged = changedName && std::strcmp(changedName, "loadPreset") == 0;
  const bool saveGlobalCalibrationChanged = changedName && std::strcmp(changedName, "saveGlobalCalibration") == 0;
  const bool saveNegativeCalibrationChanged = changedName && std::strcmp(changedName, "saveNegativeCalibration") == 0;
  const bool savePrintCalibrationChanged = changedName && std::strcmp(changedName, "savePrintCalibration") == 0;
  const bool savePairCalibrationChanged = changedName && std::strcmp(changedName, "savePairCalibration") == 0;
  const bool exportProductionCalibrationChanged = changedName && std::strcmp(changedName, "exportProductionCalibration") == 0;
  const bool loadActiveCalibrationChanged = changedName && std::strcmp(changedName, "loadActiveCalibration") == 0;
  const bool refreshHostColourDiagnosticsChanged = changedName && std::strcmp(changedName, "refreshHostColourDiagnostics") == 0;
  const bool supportAboutHelpChanged = changedName && std::strcmp(changedName, "supportAboutHelp") == 0;
  const bool supportOpenMCNexusChanged = changedName && std::strcmp(changedName, "supportOpenMCNexus") == 0;
  if (supportAboutHelpChanged) {
    if (openExternalUrl("https://github.com/ciqueira/LookFilmLab")) {
      return kOfxStatOK;
    }
    showMessage(effect, kOfxMessageWarning, "lookfilmlabSupport", "Could not open the LookFilmLab support page.");
    return kOfxStatReplyDefault;
  }
  if (supportOpenMCNexusChanged) {
    if (openMCNexusApp()) {
      return kOfxStatOK;
    }
    showMessage(effect, kOfxMessageWarning, "lookfilmlabSupport", "Could not open the MCNexus app.");
    return kOfxStatReplyDefault;
  }
  if (refreshHostColourDiagnosticsChanged) {
    updateHostColourDiagnostics(effect, data);
    return kOfxStatOK;
  }
  if (copyParamsChanged || pasteParamsChanged || saveDefaultsChanged || resetDefaultsChanged ||
      exportLutChanged || savePresetChanged || loadPresetChanged ||
      saveGlobalCalibrationChanged || saveNegativeCalibrationChanged || savePrintCalibrationChanged ||
      savePairCalibrationChanged || exportProductionCalibrationChanged || loadActiveCalibrationChanged) {
    OfxParamSetHandle paramSet = nullptr;
    gEffectHost->getParamSet(effect, &paramSet);
    OfxTime time = 0.0;
    if (inArgs) {
      gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
    }
    if (saveGlobalCalibrationChanged || saveNegativeCalibrationChanged || savePrintCalibrationChanged ||
        savePairCalibrationChanged || exportProductionCalibrationChanged) {
      const char *captureKind = "export";
      if (saveGlobalCalibrationChanged) {
        captureKind = "global";
      } else if (saveNegativeCalibrationChanged) {
        captureKind = "negative";
      } else if (savePrintCalibrationChanged) {
        captureKind = "print";
      } else if (savePairCalibrationChanged) {
        captureKind = "pair";
      }
      std::filesystem::path path;
      std::filesystem::path activePath;
      std::string error;
      if (!writeCalibrationCapture(paramSet, data, time, captureKind, path, activePath, error)) {
        showMessage(effect, kOfxMessageError, "lookfilmlabCalibration", error.empty() ? "Could not save LookFilmLab calibration." : error);
        return kOfxStatFailed;
      }
      showMessage(
        effect,
        kOfxMessageMessage,
        "lookfilmlabCalibration",
        "LookFilmLab calibration saved: " + path.string() +
          "\n\nActive production calibration master:\n" + activePath.string()
      );
      return kOfxStatOK;
    }
    if (loadActiveCalibrationChanged) {
      std::filesystem::path path;
      std::string error;
      gParamHost->paramEditBegin(paramSet, "Load active LookFilmLab calibration");
      const bool loaded = loadActiveProductionCalibration(paramSet, path, error);
      gParamHost->paramEditEnd(paramSet);
      if (!loaded) {
        showMessage(
          effect,
          kOfxMessageWarning,
          "lookfilmlabCalibration",
          error.empty() ? "No active LookFilmLab calibration master found." : error
        );
        return kOfxStatReplyDefault;
      }
      if (dirUsesStockCalibration(data)) {
        applyDirStockCalibration(data, false);
      }
      syncConditionalParamVisibility(data);
      showMessage(
        effect,
        kOfxMessageMessage,
        "lookfilmlabCalibration",
        "Active LookFilmLab calibration loaded:\n" + path.string()
      );
      return kOfxStatOK;
    }
    if (copyParamsChanged) {
      std::string error;
      if (copyVisibleParams(paramSet, time, error)) {
        return kOfxStatOK;
      }
      showMessage(effect, kOfxMessageError, "lookfilmlabDefaults", error.empty() ? "Could not copy LookFilmLab params." : error);
      return kOfxStatFailed;
    }
    if (pasteParamsChanged) {
      DefaultsSnapshot copiedParams;
      bool copyFound = false;
      std::string error;
      std::string clipboardText;
      if (!readTextFromClipboard(clipboardText, copyFound, error)) {
        showMessage(effect, kOfxMessageError, "lookfilmlabDefaults", error.empty() ? "Could not read copied LookFilmLab params." : error);
        return kOfxStatFailed;
      }
      if (!copyFound) {
        showMessage(effect, kOfxMessageWarning, "lookfilmlabDefaults", "No copied LookFilmLab params found.");
        return kOfxStatReplyDefault;
      }
      obfuscateDefaultsText(clipboardText);
      if (!decodeDefaultsSnapshot(clipboardText, copiedParams)) {
        showMessage(effect, kOfxMessageWarning, "lookfilmlabDefaults", "Clipboard does not contain LookFilmLab params.");
        return kOfxStatReplyDefault;
      }
      gParamHost->paramEditBegin(paramSet, "Paste LookFilmLab params");
      applySnapshotToParamSet(
        paramSet,
        copiedParams,
        isProProductionBuild() ? SnapshotScope::ProductionPublic : SnapshotScope::All,
        true
      );
      gParamHost->paramEditEnd(paramSet);
      syncConditionalParamVisibility(data);
      return kOfxStatOK;
    }
    if (saveDefaultsChanged) {
      std::string error;
      if (saveVisibleDefaults(paramSet, time, error)) {
        showMessage(effect, kOfxMessageMessage, "lookfilmlabDefaults", "LookFilmLab defaults saved successfully.");
        return kOfxStatOK;
      }
      showMessage(effect, kOfxMessageError, "lookfilmlabDefaults", error.empty() ? "Could not save LookFilmLab defaults." : error);
      return kOfxStatFailed;
    }
    if (savePresetChanged) {
      DefaultsSnapshot snapshot;
      captureParamSetSnapshot(
        paramSet,
        time,
        snapshot,
        isProProductionBuild() ? SnapshotScope::ProductionPublic : SnapshotScope::All,
        true
      );
      const std::string displayName = normalizedPresetName(getStringValue(data->presetName));
      const std::filesystem::path path = generatedPresetPath(displayName);
      std::string error;
      if (!writePresetFile(path, displayName, snapshot, error)) {
        showMessage(effect, kOfxMessageError, "lookfilmlabPreset", error.empty() ? "Could not save LookFilmLab preset." : error);
        return kOfxStatFailed;
      }
      if (data->presetName) {
        gParamHost->paramSetValue(data->presetName, displayName.c_str());
      }
      refreshPresetDropdown(data, path);
      showMessage(
        effect,
        kOfxMessageMessage,
        "lookfilmlabPreset",
        "LookFilmLab preset saved successfully: " + displayName + "\nPreset folder: " + presetFolder().string()
      );
      return kOfxStatOK;
    }
    if (loadPresetChanged) {
      const std::vector<PresetEntry> entries = listPresetEntries();
      if (entries.empty()) {
        showMessage(effect, kOfxMessageWarning, "lookfilmlabPreset", "No LookFilmLab presets found.\nPreset folder: " + presetFolder().string());
        refreshPresetDropdown(data);
        return kOfxStatReplyDefault;
      }
      const int selected = getIntValue(data->presetSelection, 0);
      if (selected < 0 || selected >= static_cast<int>(entries.size())) {
        showMessage(effect, kOfxMessageWarning, "lookfilmlabPreset", "Selected LookFilmLab preset is no longer available.");
        refreshPresetDropdown(data);
        return kOfxStatReplyDefault;
      }

      DefaultsSnapshot presetSnapshot;
      std::string displayName;
      std::string error;
      if (!readPresetSnapshot(entries[static_cast<size_t>(selected)].path, presetSnapshot, displayName, error)) {
        showMessage(effect, kOfxMessageError, "lookfilmlabPreset", error.empty() ? "Could not load LookFilmLab preset." : error);
        refreshPresetDropdown(data);
        return kOfxStatFailed;
      }
      gParamHost->paramEditBegin(paramSet, "Load LookFilmLab preset");
      applySnapshotToParamSet(
        paramSet,
        presetSnapshot,
        isProProductionBuild() ? SnapshotScope::ProductionPublic : SnapshotScope::All,
        true
      );
      if (data->presetName && !displayName.empty()) {
        gParamHost->paramSetValue(data->presetName, displayName.c_str());
      }
      gParamHost->paramEditEnd(paramSet);
      syncConditionalParamVisibility(data);
      refreshPresetDropdown(data, entries[static_cast<size_t>(selected)].path);
      showMessage(effect, kOfxMessageMessage, "lookfilmlabPreset", "LookFilmLab preset loaded: " + (displayName.empty() ? entries[static_cast<size_t>(selected)].displayName : displayName));
      return kOfxStatOK;
    }
    if (exportLutChanged) {
      std::filesystem::path path;
      std::vector<std::string> disabledEffects;
      std::string error;
      if (!exportCurrentLut(data, time, path, disabledEffects, error)) {
        showMessage(effect, kOfxMessageError, "lookfilmlabLutExport", error.empty() ? "Could not export LookFilmLab LUT." : error);
        return kOfxStatFailed;
      }
      std::string message = "LookFilmLab LUT exported: " + path.string();
      if (!disabledEffects.empty()) {
        message += "\n\nThis LUT contains the color-only spectral transform. Disabled for export: " + joinLabels(disabledEffects) + ".";
        showMessage(effect, kOfxMessageWarning, "lookfilmlabLutExport", message);
      } else {
        showMessage(effect, kOfxMessageMessage, "lookfilmlabLutExport", message);
      }
      return kOfxStatOK;
    }
    std::string error;
    if (!deleteDefaultsFile(error)) {
      showMessage(
        effect,
        kOfxMessageError,
        "lookfilmlabDefaults",
        error.empty() ? "Could not delete LookFilmLab defaults file." : error
      );
      return kOfxStatFailed;
    }
    gParamHost->paramEditBegin(paramSet, "Reset LookFilmLab factory defaults");
    resetParamSetToFactory(paramSet);
    if (dirUsesStockCalibration(data)) {
      applyDirStockCalibration(data, false);
    }
    gParamHost->paramEditEnd(paramSet);
    syncConditionalParamVisibility(data);
    showMessage(effect, kOfxMessageMessage, "lookfilmlabDefaults", "LookFilmLab factory defaults restored.");
    return kOfxStatOK;
  }

  syncConditionalParamVisibility(data);
  if (syncProductionCreativePrinterLights(data, changedName) == kOfxStatOK) {
    return kOfxStatOK;
  }

  const bool hdrPresetChanged = changedName && std::strcmp(changedName, "hdrPreset") == 0;
  const bool hdrControlChanged = changedName && (
    std::strcmp(changedName, "hdrTransfer") == 0 ||
    std::strcmp(changedName, "hdrReferenceWhiteNits") == 0 ||
    std::strcmp(changedName, "hdrPeakNits") == 0 ||
    std::strcmp(changedName, "hdrExposureEv") == 0 ||
    std::strcmp(changedName, "hdrToneMapping") == 0
  );
  if (hdrPresetChanged && data->hdrPreset && data->hdrTransfer && data->hdrReferenceWhiteNits &&
      data->hdrPeakNits && data->hdrToneMapping) {
    int preset = 0;
    gParamHost->paramGetValue(data->hdrPreset, &preset);
    if (preset != static_cast<int>(spektrafilm::HdrPreset::Custom)) {
      const HdrPresetValues values = hdrPresetValues(preset);
      gParamHost->paramSetValue(data->hdrTransfer, values.transfer);
      gParamHost->paramSetValue(data->hdrReferenceWhiteNits, values.referenceWhiteNits);
      gParamHost->paramSetValue(data->hdrPeakNits, values.peakNits);
      gParamHost->paramSetValue(data->hdrToneMapping, values.toneMapping);
      return kOfxStatOK;
    }
  } else if (hdrControlChanged && data->hdrPreset) {
    int preset = 0;
    gParamHost->paramGetValue(data->hdrPreset, &preset);
    if (preset != static_cast<int>(spektrafilm::HdrPreset::Custom)) {
      gParamHost->paramSetValue(data->hdrPreset, static_cast<int>(spektrafilm::HdrPreset::Custom));
      return kOfxStatOK;
    }
  }

  const bool filmChanged = changedName && std::strcmp(changedName, "film") == 0;
  const bool dirCalibrateChanged = changedName && std::strcmp(changedName, "dirCalibrateToStock") == 0;
  const bool dirCoefficientChanged = changedName && (
    std::strcmp(changedName, "dirGammaSameLayerRgb") == 0 ||
    std::strcmp(changedName, "dirGammaRToGb") == 0 ||
    std::strcmp(changedName, "dirGammaGToRb") == 0 ||
    std::strcmp(changedName, "dirGammaBToRg") == 0
  );
  if (dirCalibrateChanged) {
    return applyDirStockCalibration(data, true) ? kOfxStatOK : kOfxStatReplyDefault;
  }
  if (dirCoefficientChanged && data->dirUsesStockCalibration && !data->syncingDirCalibration) {
    gParamHost->paramSetValue(data->dirUsesStockCalibration, 0);
    return kOfxStatOK;
  }
  if (filmChanged && dirUsesStockCalibration(data)) {
    return applyDirStockCalibration(data, false) ? kOfxStatOK : kOfxStatReplyDefault;
  }

  if (!data->printerLightR || !data->printerLightG || !data->printerLightB ||
      !data->printerLightsGang || !data->printerLightsGroup) {
    return kOfxStatReplyDefault;
  }
  if (data->syncingPrinterLights) {
    return kOfxStatOK;
  }
  const bool printerLightRChanged = changedName && std::strcmp(changedName, "printerLightR") == 0;
  const bool printerLightGChanged = changedName && std::strcmp(changedName, "printerLightG") == 0;
  const bool printerLightBChanged = changedName && std::strcmp(changedName, "printerLightB") == 0;
  const bool printerLightsChanged = printerLightRChanged || printerLightGChanged || printerLightBChanged;
  const bool gangChanged = changedName && std::strcmp(changedName, "printerLightsGang") == 0;
  const bool groupChanged = changedName && std::strcmp(changedName, "printerLightsGroup") == 0;
  if (!printerLightsChanged && !gangChanged && !groupChanged) {
    return kOfxStatReplyDefault;
  }

  double current[3] = {0.0, 0.0, 0.0};
  if (!readCurrentPrinterLights(data, current)) {
    return kOfxStatReplyDefault;
  }

  bool gangEnabled = getBoolValue(data->printerLightsGang, false);
  bool groupEnabled = getBoolValue(data->printerLightsGroup, false);
  if (gangChanged && gangEnabled && groupEnabled) {
    data->syncingPrinterLights = true;
    gParamHost->paramSetValue(data->printerLightsGroup, 0);
    data->syncingPrinterLights = false;
    groupEnabled = false;
  } else if (groupChanged && groupEnabled && gangEnabled) {
    data->syncingPrinterLights = true;
    gParamHost->paramSetValue(data->printerLightsGang, 0);
    data->syncingPrinterLights = false;
    gangEnabled = false;
  }

  if (!gangEnabled && !groupEnabled) {
    rememberCurrentPrinterLights(data, current);
    return kOfxStatReplyDefault;
  }

  if (gangEnabled) {
    double linkedValue = (current[0] + current[1] + current[2]) / 3.0;
    if (printerLightRChanged) {
      linkedValue = current[0];
    } else if (printerLightGChanged) {
      linkedValue = current[1];
    } else if (printerLightBChanged) {
      linkedValue = current[2];
    }

    double linked[3] = {linkedValue, linkedValue, linkedValue};
    rememberCurrentPrinterLights(data, linked);
    data->syncingPrinterLights = true;
    if (std::abs(current[0] - linkedValue) > 1.0e-9) {
      gParamHost->paramSetValue(data->printerLightR, linkedValue);
    }
    if (std::abs(current[1] - linkedValue) > 1.0e-9) {
      gParamHost->paramSetValue(data->printerLightG, linkedValue);
    }
    if (std::abs(current[2] - linkedValue) > 1.0e-9) {
      gParamHost->paramSetValue(data->printerLightB, linkedValue);
    }
    data->syncingPrinterLights = false;
    return kOfxStatOK;
  }

  if (!printerLightsChanged || !data->lastPrinterLightsInitialized) {
    rememberCurrentPrinterLights(data, current);
    return kOfxStatReplyDefault;
  }

  int changedIndex = 0;
  if (printerLightGChanged) {
    changedIndex = 1;
  } else if (printerLightBChanged) {
    changedIndex = 2;
  }
  const double delta = current[changedIndex] - data->lastPrinterLights[changedIndex];
  if (std::abs(delta) <= 1.0e-9) {
    rememberCurrentPrinterLights(data, current);
    return kOfxStatReplyDefault;
  }

  double grouped[3] = {
    std::clamp(data->lastPrinterLights[0] + delta, -24.0, 24.0),
    std::clamp(data->lastPrinterLights[1] + delta, -24.0, 24.0),
    std::clamp(data->lastPrinterLights[2] + delta, -24.0, 24.0),
  };
  grouped[changedIndex] = current[changedIndex];
  rememberCurrentPrinterLights(data, grouped);
  data->syncingPrinterLights = true;
  if (std::abs(current[0] - grouped[0]) > 1.0e-9) {
    gParamHost->paramSetValue(data->printerLightR, grouped[0]);
  }
  if (std::abs(current[1] - grouped[1]) > 1.0e-9) {
    gParamHost->paramSetValue(data->printerLightG, grouped[1]);
  }
  if (std::abs(current[2] - grouped[2]) > 1.0e-9) {
    gParamHost->paramSetValue(data->printerLightB, grouped[2]);
  }
  data->syncingPrinterLights = false;
  return kOfxStatOK;
}

double filmFormatMm(spektrafilm::FilmFormat format) {
  switch (format) {
    case spektrafilm::FilmFormat::Standard8:
      return 4.8;
    case spektrafilm::FilmFormat::Super8:
      return 5.79;
    case spektrafilm::FilmFormat::Standard16:
      return 10.26;
    case spektrafilm::FilmFormat::Super16:
      return 12.52;
    case spektrafilm::FilmFormat::Super35:
      return 24.89;
    case spektrafilm::FilmFormat::Standard65:
      return 52.48;
    case spektrafilm::FilmFormat::Imax70:
      return 70.41;
    case spektrafilm::FilmFormat::Standard35:
    default:
      return 35.0;
  }
}

double grainFinalBlurFormatScale(spektrafilm::FilmFormat format) {
  return std::pow(std::max(filmFormatMm(format) / 35.0, 1.0e-6), 0.62);
}

double effectiveGrainFinalBlurUm(const spektrafilm::RenderParams &params) {
  return std::max(static_cast<double>(params.grainFinalBlurUm), 0.0) *
    grainFinalBlurFormatScale(params.filmFormat);
}

double enlargerScale(const spektrafilm::RenderParams &params) {
  return std::clamp(static_cast<double>(params.enlargerScale), 1.0, 32.0);
}

bool enlargerTransformActive(const spektrafilm::RenderParams &params) {
  return std::abs(enlargerScale(params) - 1.0) > 1.0e-6;
}

double rectWidth(const OfxRectD &rect) {
  return std::max(rect.x2 - rect.x1, 0.0);
}

double rectHeight(const OfxRectD &rect) {
  return std::max(rect.y2 - rect.y1, 0.0);
}

double sourceLongEdgePixels(InstanceData *data, OfxTime time, const OfxRectD &roi) {
  double longEdge = std::max({rectWidth(roi), rectHeight(roi), 1.0});
  if (data && data->sourceClip && gEffectHost) {
    OfxRectD sourceRod{};
    if (gEffectHost->clipGetRegionOfDefinition(data->sourceClip, time, &sourceRod) == kOfxStatOK) {
      longEdge = std::max({longEdge, rectWidth(sourceRod), rectHeight(sourceRod), 1.0});
    }
  }
  return longEdge;
}

double pixelSizeUmForRender(InstanceData *data, OfxTime time, const spektrafilm::RenderParams &params, const OfxRectD &roi) {
  const double formatLongEdgeMm = filmFormatMm(params.filmFormat);
  const double scale = enlargerScale(params);
  return formatLongEdgeMm * 1000.0 / sourceLongEdgePixels(data, time, roi) / scale;
}

double normalQuantile(double p) {
  p = std::clamp(p, 1.0e-9, 1.0 - 1.0e-9);
  constexpr double a1 = -3.969683028665376e+01;
  constexpr double a2 = 2.209460984245205e+02;
  constexpr double a3 = -2.759285104469687e+02;
  constexpr double a4 = 1.383577518672690e+02;
  constexpr double a5 = -3.066479806614716e+01;
  constexpr double a6 = 2.506628277459239e+00;
  constexpr double b1 = -5.447609879822406e+01;
  constexpr double b2 = 1.615858368580409e+02;
  constexpr double b3 = -1.556989798598866e+02;
  constexpr double b4 = 6.680131188771972e+01;
  constexpr double b5 = -1.328068155288572e+01;
  constexpr double c1 = -7.784894002430293e-03;
  constexpr double c2 = -3.223964580411365e-01;
  constexpr double c3 = -2.400758277161838e+00;
  constexpr double c4 = -2.549732539343734e+00;
  constexpr double c5 = 4.374664141464968e+00;
  constexpr double c6 = 2.938163982698783e+00;
  constexpr double d1 = 7.784695709041462e-03;
  constexpr double d2 = 3.224671290700398e-01;
  constexpr double d3 = 2.445134137142996e+00;
  constexpr double d4 = 3.754408661907416e+00;
  constexpr double pLow = 0.02425;
  constexpr double pHigh = 1.0 - pLow;
  if (p < pLow) {
    const double q = std::sqrt(-2.0 * std::log(p));
    return (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
      ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
  }
  if (p > pHigh) {
    const double q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
      ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
  }
  const double q = p - 0.5;
  const double r = q * q;
  return (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q /
    (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1.0);
}

double grainSynthesisMaxRadiusUm(const spektrafilm::RenderParams &params) {
  const double mean = std::max(
    static_cast<double>(params.grainSynthesisMeanRadiusUm) *
      std::clamp(static_cast<double>(params.grainSynthesisSize), 0.25, 4.0),
    1.0e-6
  );
  const double ratio = std::max(static_cast<double>(params.grainSynthesisRadiusStdDevRatio), 0.0);
  double radius = mean;
  if (ratio > 1.0e-6) {
    const double varianceRatio = ratio * ratio;
    const double sigma = std::sqrt(std::log(1.0 + varianceRatio));
    const double mu = std::log(mean) - 0.5 * sigma * sigma;
    radius = std::exp(mu + sigma * normalQuantile(params.grainSynthesisMaxRadiusQuantile));
  }
  const double channelScale = std::max({
    static_cast<double>(params.grainSynthesisRadiusScaleR),
    static_cast<double>(params.grainSynthesisRadiusScaleG),
    static_cast<double>(params.grainSynthesisRadiusScaleB),
    1.0e-6
  });
  const double layerScale = params.grainSynthesisLayered
    ? std::max({
        static_cast<double>(params.grainSynthesisLayerScale0),
        static_cast<double>(params.grainSynthesisLayerScale1),
        static_cast<double>(params.grainSynthesisLayerScale2),
        1.0e-6
      })
    : 1.0;
  return radius * channelScale * layerScale;
}

double grainSynthesisObservationSigmaUm(const spektrafilm::RenderParams &params) {
  return std::max(static_cast<double>(params.grainSynthesisObservationSigmaUm), 0.0) /
    std::max(static_cast<double>(params.grainSynthesisSharpness), 0.25);
}

double scannerSigmaUmFromMtf50(double mtf50LpMm) {
  if (!std::isfinite(mtf50LpMm) || mtf50LpMm <= 0.0) {
    return 0.0;
  }
  constexpr double kPi = 3.14159265358979323846;
  return 1000.0 * std::sqrt(std::log(2.0) / (2.0 * kPi * kPi)) / mtf50LpMm;
}

int clampedKernelRadius(double sigmaPixels, int cap) {
  if (!std::isfinite(sigmaPixels) || sigmaPixels <= 0.0) {
    return 0;
  }
  return std::clamp(static_cast<int>(std::ceil(3.0 * sigmaPixels)), 0, cap);
}

double diffusionGroupMaxLambdaUm(spektrafilm::DiffusionFilterFamily family, int group, double size) {
  double lambdaUm = 0.0;
  double spread = 1.0;
  switch (family) {
    case spektrafilm::DiffusionFilterFamily::Glimmerglass:
      lambdaUm = group == 0 ? 10.0 : group == 1 ? 50.0 : 260.0;
      break;
    case spektrafilm::DiffusionFilterFamily::ProMist:
      lambdaUm = group == 0 ? 14.0 : group == 1 ? 150.0 : 650.0;
      break;
    case spektrafilm::DiffusionFilterFamily::CineBloom:
      lambdaUm = group == 0 ? 20.0 : group == 1 ? 200.0 : 1000.0;
      break;
    case spektrafilm::DiffusionFilterFamily::BlackProMist:
    default:
      lambdaUm = group == 0 ? 16.0 : group == 1 ? 95.0 : 380.0;
      break;
  }
  spread = group == 0 ? 1.5 : group == 1 ? 2.0 : 2.5;
  return lambdaUm * spread * std::max(size, 1.0e-6);
}

int diffusionRadiusPixels(
  spektrafilm::DiffusionFilterFamily family,
  double strength,
  double spatialScale,
  double coreIntensity,
  double coreSize,
  double haloIntensity,
  double haloSize,
  double bloomIntensity,
  double bloomSize,
  double pixelSizeUm
) {
  if (strength <= 0.0 || spatialScale <= 0.0 || pixelSizeUm <= 0.0) {
    return 0;
  }
  double maxLambdaUm = 0.0;
  if (coreIntensity > 0.0) {
    maxLambdaUm = std::max(maxLambdaUm, diffusionGroupMaxLambdaUm(family, 0, coreSize));
  }
  if (haloIntensity > 0.0) {
    maxLambdaUm = std::max(maxLambdaUm, diffusionGroupMaxLambdaUm(family, 1, haloSize));
  }
  if (bloomIntensity > 0.0) {
    maxLambdaUm = std::max(maxLambdaUm, diffusionGroupMaxLambdaUm(family, 2, bloomSize));
  }
  constexpr double kMaxExpGaussianFitSigmaScale = 2.7684;
  return clampedKernelRadius(maxLambdaUm * kMaxExpGaussianFitSigmaScale * spatialScale / pixelSizeUm, 256);
}

int halationRadiusPixels(const spektrafilm::RenderParams &params, double pixelSizeUm) {
  if (!params.halationEnabled || pixelSizeUm <= 0.0) {
    return 0;
  }
  double maxSigmaUm = 0.0;
  if (params.scatterAmount > 0.0f && params.scatterScale > 0.0f) {
    constexpr double kMaxScatterTailUm = 9.7;
    constexpr double kMaxExpGaussianFitSigmaScale = 2.7684;
    maxSigmaUm = std::max(maxSigmaUm, kMaxScatterTailUm * kMaxExpGaussianFitSigmaScale * params.scatterScale);
  }
  if (params.halationAmount > 0.0f && params.halationScale > 0.0f) {
    constexpr double kMaxProfileFirstSigmaUm = 65.0;
    constexpr double kThirdBounceSigmaScale = 1.7320508075688772;
    maxSigmaUm = std::max(maxSigmaUm, kMaxProfileFirstSigmaUm * kThirdBounceSigmaScale * params.halationScale);
  }
  return clampedKernelRadius(maxSigmaUm / pixelSizeUm, 256);
}

int dirRadiusPixels(const spektrafilm::RenderParams &params, double pixelSizeUm) {
  if (params.dirCouplersAmount <= 0.0f || params.dirCouplersDiffusionUm <= 0.0f || pixelSizeUm <= 0.0) {
    return 0;
  }
  double maxSigmaUm = params.dirCouplersDiffusionUm;
  if (params.dirCouplersDiffusionTailUm > 0.0f && params.dirCouplersDiffusionTailWeight > 0.0f) {
    constexpr double kMaxExpGaussianFitSigmaScale = 2.7684;
    maxSigmaUm = std::max(maxSigmaUm, static_cast<double>(params.dirCouplersDiffusionTailUm) * kMaxExpGaussianFitSigmaScale);
  }
  return clampedKernelRadius(maxSigmaUm / pixelSizeUm, 256);
}

int grainRadiusPixels(const spektrafilm::RenderParams &params, double pixelSizeUm) {
  if (!params.grainEnabled) {
    return 0;
  }
  if (params.grainModel == spektrafilm::GrainModel::GrainSynthesis && pixelSizeUm > 0.0) {
    const double supportUm = grainSynthesisMaxRadiusUm(params) +
      3.0 * grainSynthesisObservationSigmaUm(params);
    return clampedKernelRadius(std::max(supportUm, effectiveGrainFinalBlurUm(params)) / pixelSizeUm, 256);
  }
  double maxSigmaPixels = pixelSizeUm > 0.0
    ? effectiveGrainFinalBlurUm(params) / pixelSizeUm
    : 0.0;
  if (params.grainModel == spektrafilm::GrainModel::Production && pixelSizeUm > 0.0) {
    maxSigmaPixels = std::max(maxSigmaPixels, static_cast<double>(params.grainBlurDyeCloudsUm) / pixelSizeUm);
    maxSigmaPixels = std::max(maxSigmaPixels, static_cast<double>(params.grainMicroStructureScale) / pixelSizeUm);
  }
  return clampedKernelRadius(maxSigmaPixels, 64);
}

int scannerRadiusPixels(const spektrafilm::RenderParams &params, double pixelSizeUm) {
  if (!params.scannerEnabled || pixelSizeUm <= 0.0) {
    return 0;
  }
  return std::max(
    clampedKernelRadius(scannerSigmaUmFromMtf50(params.scannerMtf50LpMm) / pixelSizeUm, 256),
    clampedKernelRadius(std::max(static_cast<double>(params.scannerUnsharpRadiusUm), 0.0) / pixelSizeUm, 256)
  );
}

int estimateSourceExpansionPixels(InstanceData *data, OfxTime time, const spektrafilm::RenderParams &params, const OfxRectD &roi) {
  const double pixelSizeUm = pixelSizeUmForRender(data, time, params, roi);
  int radius = 0;
  radius = std::max(radius, halationRadiusPixels(params, pixelSizeUm));
  radius = std::max(radius, dirRadiusPixels(params, pixelSizeUm));
  radius = std::max(radius, grainRadiusPixels(params, pixelSizeUm));
  radius = std::max(radius, scannerRadiusPixels(params, pixelSizeUm));
  if (params.cameraDiffusionEnabled) {
    radius = std::max(radius, diffusionRadiusPixels(
      params.cameraDiffusionFamily,
      params.cameraDiffusionStrength,
      params.cameraDiffusionSpatialScale,
      params.cameraDiffusionCoreIntensity,
      params.cameraDiffusionCoreSize,
      params.cameraDiffusionHaloIntensity,
      params.cameraDiffusionHaloSize,
      params.cameraDiffusionBloomIntensity,
      params.cameraDiffusionBloomSize,
      pixelSizeUm
    ));
  }
  if (params.printDiffusionEnabled) {
    radius = std::max(radius, diffusionRadiusPixels(
      params.printDiffusionFamily,
      params.printDiffusionStrength,
      params.printDiffusionSpatialScale,
      params.printDiffusionCoreIntensity,
      params.printDiffusionCoreSize,
      params.printDiffusionHaloIntensity,
      params.printDiffusionHaloSize,
      params.printDiffusionBloomIntensity,
      params.printDiffusionBloomSize,
      pixelSizeUm
    ));
  }
  return radius;
}

OfxRectD mapRoiThroughEnlarger(
  const spektrafilm::RenderParams &params,
  const OfxRectD &roi,
  const OfxRectD &sourceRod
) {
  if (!enlargerTransformActive(params)) {
    return roi;
  }
  const double width = std::max(rectWidth(sourceRod), 1.0);
  const double height = std::max(rectHeight(sourceRod), 1.0);
  const double scale = enlargerScale(params);
  const double offsetX = static_cast<double>(params.enlargerOffsetXPercent) * 0.01 / scale;
  const double offsetY = static_cast<double>(params.enlargerOffsetYPercent) * 0.01 / scale;
  const auto mapX = [&](double x) {
    const double normalized = (x - sourceRod.x1) / width;
    return sourceRod.x1 + (0.5 + (normalized - 0.5) / scale + offsetX) * width;
  };
  const auto mapY = [&](double y) {
    const double normalized = (y - sourceRod.y1) / height;
    return sourceRod.y1 + (0.5 + (normalized - 0.5) / scale + offsetY) * height;
  };
  OfxRectD mapped{};
  mapped.x1 = std::min(mapX(roi.x1), mapX(roi.x2));
  mapped.x2 = std::max(mapX(roi.x1), mapX(roi.x2));
  mapped.y1 = std::min(mapY(roi.y1), mapY(roi.y2));
  mapped.y2 = std::max(mapY(roi.y1), mapY(roi.y2));
  return mapped;
}

OfxStatus getRegionOfDefinition(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  InstanceData *data = getInstanceData(effect);
  OfxTime time = 0.0;
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  OfxRectD rod{};
  gEffectHost->clipGetRegionOfDefinition(data->sourceClip, time, &rod);
  gPropHost->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, &rod.x1);
  return kOfxStatOK;
}

OfxStatus getRegionOfInterest(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  InstanceData *data = getInstanceData(effect);
  OfxTime time = 0.0;
  OfxRectD roi{};
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  gPropHost->propGetDoubleN(inArgs, kOfxImageEffectPropRegionOfInterest, 4, &roi.x1);

  if (data) {
    const spektrafilm::RenderParams params = readParams(data, time);
    const int expansion = estimateSourceExpansionPixels(data, time, params, roi);
    OfxRectD sourceRod{};
    if (data->sourceClip && gEffectHost->clipGetRegionOfDefinition(data->sourceClip, time, &sourceRod) == kOfxStatOK) {
      roi = mapRoiThroughEnlarger(params, roi, sourceRod);
      const double sourceExpansion = std::ceil(static_cast<double>(expansion) / enlargerScale(params));
      roi.x1 -= sourceExpansion;
      roi.y1 -= sourceExpansion;
      roi.x2 += sourceExpansion;
      roi.y2 += sourceExpansion;
      roi.x1 = std::max(roi.x1, sourceRod.x1);
      roi.y1 = std::max(roi.y1, sourceRod.y1);
      roi.x2 = std::min(roi.x2, sourceRod.x2);
      roi.y2 = std::min(roi.y2, sourceRod.y2);
    } else {
      roi.x1 -= expansion;
      roi.y1 -= expansion;
      roi.x2 += expansion;
      roi.y2 += expansion;
    }
  }

  gPropHost->propSetDoubleN(outArgs, "OfxImageClipPropRoI_Source", 4, &roi.x1);
  return kOfxStatOK;
}

OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle) {
  InstanceData *data = getInstanceData(effect);
  if (!data) {
    return kOfxStatFailed;
  }

  OfxTime time = 0.0;
  OfxRectI renderWindow{};
  gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
  gPropHost->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);
  spektrafilm::RenderWindow window{renderWindow.x1, renderWindow.y1, renderWindow.x2, renderWindow.y2};
  spektrafilm::RenderParams params = readParams(data, time);

#if SPEKTRAFILM_OFX_METAL_GPU_BUFFERS
  if (metalGpuBuffersEnabled(inArgs)) {
    std::lock_guard<std::mutex> rendererLock(data->rendererMutex);
    if (!ensureRenderer(data)) {
      return kOfxStatFailed;
    }
    return probeMetalGpuBufferRender(effect, data, time, inArgs, window, params);
  }
#endif

  OfxPropertySetHandle sourceImage = nullptr;
  OfxPropertySetHandle outputImage = nullptr;
  spektrafilm::ImageView source{};
  spektrafilm::MutableImageView output{};
  OfxStatus status = kOfxStatOK;

  try {
    status = fetchImageView(data->sourceClip, time, &sourceImage, source);
    if (status != kOfxStatOK) {
      throw status;
    }
    status = fetchMutableImageView(data->outputClip, time, &outputImage, output);
    if (status != kOfxStatOK) {
      throw status;
    }

    std::lock_guard<std::mutex> rendererLock(data->rendererMutex);
    spektrafilm::Renderer *renderer = ensureRenderer(data);
    if (!renderer) {
      throw kOfxStatFailed;
    }
    if (!renderer->render(source, output, window, params, time)) {
      if (gMessageHost) {
        gMessageHost->message(effect, kOfxMessageError, "lookfilmlabRenderer", "%s", renderer->lastError().c_str());
      }
      status = kOfxStatFailed;
    }
  } catch (OfxStatus caught) {
    status = caught;
  } catch (const std::bad_alloc &) {
    status = kOfxStatErrMemory;
  } catch (...) {
    status = kOfxStatErrUnknown;
  }

  releaseImage(sourceImage);
  releaseImage(outputImage);
  return gEffectHost->abort(effect) ? kOfxStatOK : status;
}

OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle) {
  OfxPropertySetHandle props = nullptr;
  gEffectHost->clipDefine(effect, kOfxImageEffectOutputClipName, &props);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  gEffectHost->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &props);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

  OfxParamSetHandle paramSet = nullptr;
  gEffectHost->getParamSet(effect, &paramSet);

  DefaultsSnapshot savedDefaults;
  bool defaultsFound = false;
  std::string defaultsError;
  if (loadDefaultsFromFile(savedDefaults, defaultsFound, defaultsError) && defaultsFound) {
    gDescribeDefaults = &savedDefaults;
  }

  defineGroup(paramSet, "colorGroup", "Color Management", true);
  defineGroup(paramSet, "printSourceGroup", "", true);
  defineGroup(paramSet, "filmScanGroup", "Film Scan", true);
  if (isProductScan()) {
    defineGroup(paramSet, "scannedNegativeGroup", "Scanned Negative", true);
  }
  defineGroup(paramSet, "productionCameraGroup", "Camera Settings", true);
  defineGroup(paramSet, "productionStocksGroup", "Film Stocks", true);
  defineGroup(paramSet, "productionLaboratoryGroup", "Laboratory", true);
  defineGroup(paramSet, "filteringGroup", "Filtering", false);
  defineGroup(paramSet, "enlargerGroup", "Film Plane", false);
  defineGroup(paramSet, "filmGroup", "Film", true);
  defineGroup(paramSet, "printGroup", "Print", true);
  defineGroup(paramSet, "couplerGroup", "DIR Couplers", false);
  defineGroup(paramSet, "calibrationGroup", "Calibration", false);
  defineGroup(paramSet, "grainGroup", "Grain", true);
  defineGroup(paramSet, "grainSynthesisGroup", "Grain Synthesis", false);
  defineGroup(paramSet, "halationGroup", "Halation", false);
  defineGroup(paramSet, "diffusionGroup", "Diffusion", false);
  defineGroup(paramSet, "scannerGroup", "Scanner", false);
  defineGroup(paramSet, "infoGroup", "Info", false);
  defineGroup(paramSet, "manageGroup", "Manage", false);
  defineGroup(paramSet, "supportGroup", "Suport", false);

  const char *processOptions[] = {"Print simulation", "Scan negative", "Process negative"};
  defineChoice(paramSet, "process", "Mode", processOptions, 3, 0, "colorGroup");
  defineBool(paramSet, "scanNegativeInvert", "Invert Negative Scan", false, "colorGroup");
  const char *rgbToRawOptions[] = {"Hanatos 2026", "Hanatos 2025", "Mallett 2019"};
  defineChoice(paramSet, "rgbToRawMethod", "RGB to Raw", rgbToRawOptions, 3, 0, "filmGroup");
  const char *colorSpaces[] = {
    "ARRI Wide Gamut 4 / LogC4",
    "ARRI Wide Gamut 3 / LogC3",
    "Blackmagic Wide Gamut / Film Gen 5",
    "DaVinci Wide Gamut / Intermediate",
    "REDWideGamutRGB / Log3G10",
    "S-Gamut3 / S-Log3",
    "S-Gamut3.Cine / S-Log3",
    "Cinema Gamut D55 / Canon Log 2",
    "Cinema Gamut D55 / Canon Log 3",
    "V-Gamut / V-Log",
    "ACES2065-1 / Linear",
    "ACEScg / Linear",
    "ACEScg / ACEScct",
    "ACEScg / ACEScc",
    "Rec.2020 / Linear",
    "Rec.709 / Linear",
    "P3-D65 / Linear",
    "sRGB / sRGB",
    "Display P3 / sRGB",
    "ProPhoto RGB / ProPhoto",
    "Adobe RGB (1998) / Gamma 2.199",
    "DCI-P3 / Gamma 2.6",
    "P3-D65 / Gamma 2.2",
    "P3-D65 / Gamma 2.6",
    "Rec.709 / Gamma 2.2",
    "Rec.709 / Gamma 2.4"
  };
  const char *rcmInputColorSpaces[] = {
    "DaVinci Wide Gamut / Intermediate",
    "Blackmagic Wide Gamut / Film Gen 5",
    "ACES2065-1 / Linear",
    "ACEScg / Linear",
    "ACEScg / ACEScct",
    "ACEScg / ACEScc",
    "Rec.2020 / Linear",
    "Rec.709 / Linear",
    "P3-D65 / Linear",
    "Rec.709 / Gamma 2.4",
    "Rec.709 / Gamma 2.2",
    "sRGB / sRGB",
    "Display P3 / sRGB",
    "P3-D65 / Gamma 2.2",
    "P3-D65 / Gamma 2.6",
    "DCI-P3 / Gamma 2.6"
  };
  const char *sceneOutputColorSpaces[] = {
    "DaVinci Wide Gamut / Intermediate"
  };
  const char *sdrOutputColorSpaces[] = {
    "sRGB / sRGB",
    "Display P3 / sRGB",
    "ProPhoto RGB / ProPhoto",
    "Adobe RGB (1998) / Gamma 2.199",
    "DCI-P3 / Gamma 2.6",
    "P3-D65 / Gamma 2.2",
    "P3-D65 / Gamma 2.6",
    "Rec.709 / Gamma 2.2",
    "Rec.709 / Gamma 2.4"
  };
  const char *primariesColorSpaces[] = {
    "ARRI Wide Gamut 4",
    "ARRI Wide Gamut 3",
    "Blackmagic Wide Gamut",
    "DaVinci Wide Gamut",
    "REDWideGamutRGB",
    "S-Gamut3",
    "S-Gamut3.Cine",
    "Canon Cinema Gamut D55",
    "V-Gamut",
    "ACES2065-1",
    "ACEScg",
    "Rec.2020",
    "Rec.709 / sRGB",
    "P3-D65",
    "Display P3",
    "ProPhoto RGB",
    "Adobe RGB (1998)",
    "DCI-P3"
  };
  const char *transferColorSpaces[] = {
    "ARRI LogC4",
    "ARRI LogC3",
    "Blackmagic Film Gen 5",
    "DaVinci Intermediate",
    "RED Log3G10",
    "S-Log3",
    "Canon Log 2",
    "Canon Log 3",
    "V-Log",
    "Linear",
    "ACEScct",
    "ACEScc",
    "sRGB",
    "ProPhoto",
    "Adobe RGB Gamma 2.199",
    "Gamma 2.6",
    "Gamma 2.2",
    "Gamma 2.4"
  };
  defineChoice(paramSet, "inputColorSpace", "Legacy Input Color Space", colorSpaces, static_cast<int>(sizeof(colorSpaces) / sizeof(colorSpaces[0])), 1, "colorGroup");
  defineChoice(paramSet, "inputPrimariesColorSpace", "Input Color Space", primariesColorSpaces, static_cast<int>(sizeof(primariesColorSpaces) / sizeof(primariesColorSpaces[0])), 1, "colorGroup");
  defineChoice(paramSet, "inputTransferColorSpace", "Input Gamma", transferColorSpaces, static_cast<int>(sizeof(transferColorSpaces) / sizeof(transferColorSpaces[0])), 1, "colorGroup");
  defineChoice(paramSet, "rcmInputColorSpace", "Input Color Space", rcmInputColorSpaces, static_cast<int>(sizeof(rcmInputColorSpaces) / sizeof(rcmInputColorSpaces[0])), 0, "colorGroup");
  const char *outputRoles[] = {"Display Out SDR", "Display Out HDR", "RCM/ACES (Beta)"};
  defineChoice(paramSet, "outputRole", "Output Role", outputRoles, outputRoleOptionCountForFlavor(), 0, "colorGroup");
  defineChoice(paramSet, "sdrOutputColorSpace", "Legacy Output Color Space", sdrOutputColorSpaces, static_cast<int>(sizeof(sdrOutputColorSpaces) / sizeof(sdrOutputColorSpaces[0])), 8, "colorGroup");
  defineChoice(paramSet, "outputPrimariesColorSpace", "Output Color Space", primariesColorSpaces, static_cast<int>(sizeof(primariesColorSpaces) / sizeof(primariesColorSpaces[0])), 12, "colorGroup");
  defineChoice(paramSet, "outputTransferColorSpace", "Output Gamma", transferColorSpaces, static_cast<int>(sizeof(transferColorSpaces) / sizeof(transferColorSpaces[0])), 17, "colorGroup");
  defineChoice(paramSet, "sceneOutputColorSpace", "Output Color Space", sceneOutputColorSpaces, static_cast<int>(sizeof(sceneOutputColorSpaces) / sizeof(sceneOutputColorSpaces[0])), 0, "colorGroup");
  const char *hdrPresets[] = {"PQ 1000", "PQ 4000", "HLG 1000", "Custom"};
  defineChoice(paramSet, "hdrPreset", "HDR Preset", hdrPresets, 4, 0, "colorGroup");
  const char *hdrTransfers[] = {"Rec.2100 ST2084 (PQ)", "Rec.2100 HLG"};
  defineChoice(paramSet, "hdrTransfer", "HDR Transfer", hdrTransfers, 2, 0, "colorGroup");
  defineDouble(paramSet, "hdrReferenceWhiteNits", "Reference White Nits", 203.0, 48.0, 1000.0, "colorGroup");
  defineDouble(paramSet, "hdrPeakNits", "Peak Nits", 1000.0, 100.0, 10000.0, "colorGroup");
  defineDouble(paramSet, "hdrExposureEv", "HDR Exposure EV", 0.0, -8.0, 8.0, "colorGroup");
  const char *hdrToneMappings[] = {"Soft Rolloff", "Hard Clip"};
  defineChoice(paramSet, "hdrToneMapping", "HDR Tone Mapping", hdrToneMappings, 2, 1, "colorGroup");
  defineBool(paramSet, "colorAdaptation", "Color Adaptation", false, "colorGroup");
  defineBool(paramSet, "colorAdaptationInputCompression", "Input Compression", true, "colorGroup");
  defineBool(paramSet, "colorAdaptationCurveSmoothing", "Curve Smoothing", true, "colorGroup");
  defineBool(paramSet, "colorAdaptationOutputLightnessCompression", "Output Lightness Compression", true, "colorGroup");
  defineBool(paramSet, "colorAdaptationOutputChromaCompression", "Output Chroma Compression", true, "colorGroup");
  defineBool(paramSet, "cameraUvFilterEnabled", "Filter UV", false, "filteringGroup");
  defineDouble(paramSet, "cameraUvCutNm", "UV Cut nm", 410.0, 380.0, 450.0, "filteringGroup");
  defineBool(paramSet, "cameraIrFilterEnabled", "Filter IR", false, "filteringGroup");
  defineDouble(paramSet, "cameraIrCutNm", "IR Cut nm", 675.0, 600.0, 780.0, "filteringGroup");

  const char *const *productionNegativeProfiles = isProductPhoto()
    ? kPhotoNegativeProfileLabels
    : kCineNegativeProfileLabels;
  const int productionNegativeProfileCount = static_cast<int>(
    isProductPhoto() ? std::size(kPhotoNegativeProfileLabels) : std::size(kCineNegativeProfileLabels)
  );
  const char *const *productionPrintProfiles = kCinePrintProfileLabels;
  int productionPrintProfileCount = static_cast<int>(std::size(kCinePrintProfileLabels));
  if (isProductScan()) {
    productionPrintProfiles = kScanPaperProfileLabels;
    productionPrintProfileCount = static_cast<int>(std::size(kScanPaperProfileLabels));
  } else if (isProductPhoto()) {
    productionPrintProfiles = kPhotoPaperProfileLabels;
    productionPrintProfileCount = static_cast<int>(std::size(kPhotoPaperProfileLabels));
  }
  defineChoice(
    paramSet,
    "productionProfileNegative",
    "Profile Negative",
    productionNegativeProfiles,
    productionNegativeProfileCount,
    0,
    "productionStocksGroup"
  );
  defineChoice(
    paramSet,
    "productionProfilePrint",
    "Profile Print",
    productionPrintProfiles,
    productionPrintProfileCount,
    0,
    "productionStocksGroup"
  );

  std::vector<const char *> films;
  films.reserve(spektrafilm::kSpektraFilmCount);
  for (uint32_t i = 0; i < spektrafilm::kSpektraFilmCount; ++i) {
    const spektrafilm::ProfileCurveSet *profile = spektrafilm::filmProfileCurves(static_cast<int32_t>(i));
    films.push_back(profile && profile->name ? profile->name : "Unknown Film");
  }
  defineChoice(paramSet, "film", "Stock", films.data(), static_cast<int>(films.size()), static_cast<int>(spektrafilm::kSpektraDefaultFilmIndex), "filmGroup");
  const char *filmFormats[] = {"8mm", "Super 8", "16mm", "Super 16", "35mm", "Super 35", "65mm", "70mm / IMAX"};
  defineChoice(paramSet, "filmFormat", "Film Format", filmFormats, static_cast<int>(sizeof(filmFormats) / sizeof(filmFormats[0])), 4, isProProductionBuild() ? "productionCameraGroup" : "filmGroup");
  const char *pushPullModes[] = {"Standard", "Experimental"};
  defineChoice(paramSet, "filmPushPullMode", "Push / Pull Mode", pushPullModes, 2, 0, "filmGroup");
  defineDouble(paramSet, "filmPushPullStops", "Film Push / Pull Stops", 0.0, -2.0, 2.0, isProProductionBuild() ? "productionLaboratoryGroup" : "filmGroup");
  defineDouble(paramSet, "negativeBleachBypassAmount", "Negative Bleach Bypass", 0.0, 0.0, 1.0, isProProductionBuild() ? "productionLaboratoryGroup" : "filmGroup");
  defineDouble(paramSet, "negativeLeucoCyanCoupling", "Leuco-Cyan Coupling", 1.0, 0.0, 2.0, "filmGroup");

  std::vector<const char *> papers;
  papers.reserve(spektrafilm::kSpektraPaperCount);
  for (uint32_t i = 0; i < spektrafilm::kSpektraPaperCount; ++i) {
    const spektrafilm::ProfileCurveSet *profile = spektrafilm::paperProfileCurves(static_cast<int32_t>(i));
    papers.push_back(profile && profile->name ? profile->name : "Unknown Paper");
  }
  defineChoice(paramSet, "paper", "Paper", papers.data(), static_cast<int>(papers.size()), static_cast<int>(spektrafilm::kSpektraDefaultPaperIndex), "printGroup");
  if constexpr (spektrafilm::kSpektraAcademyPrinterDensityEnabled) {
    const char *printTimingModes[] = {"Filtered Enlarger", "Printer Density"};
    defineChoice(paramSet, "printTiming", "Print Timing", printTimingModes, 2, 0, "printGroup");
  } else {
    const char *printTimingModes[] = {"Filtered Enlarger"};
    defineChoice(paramSet, "printTiming", "Print Timing", printTimingModes, 1, 0, "printGroup");
  }
  if (!isProProductionBuild() || isProductScan()) {
    const char *scanPublicGroup = isProductScan() ? "scannedNegativeGroup" : "filmScanGroup";
    const char *printSourceOptions[] = {"Full Pipeline", "Scanned Negative / Bypass Negative"};
    if (!isProProductionBuild()) {
      defineChoice(paramSet, "printSource", "Print Source", printSourceOptions, 2, 0, "printSourceGroup");
    }
    const char *scanInputEncodingOptions[] = {"Linear", "sRGB", "Rec.709 / Gamma 2.4", "Rec.709 / Gamma 2.2"};
    defineChoice(paramSet, "scanInputEncoding", "Scan Input Encoding", scanInputEncodingOptions, 4, 0, "filmScanGroup");
    const char *scanInputColorSpaceOptions[] = {
      "sRGB/Rec.709 / Linear",
      "sRGB / sRGB",
      "Adobe RGB (1998) / Gamma 2.199",
      "Display P3 / sRGB",
      "ProPhoto RGB / ProPhoto",
      "Rec.709 / Gamma 2.4",
      "Rec.709 / Gamma 2.2",
      "P3-D65 / Linear",
      "Rec.2020 / Linear",
      "ACEScg / Linear"
    };
    defineChoice(
      paramSet,
      "scanInputColorSpace",
      "Scan Input Color Space",
      scanInputColorSpaceOptions,
      static_cast<int>(sizeof(scanInputColorSpaceOptions) / sizeof(scanInputColorSpaceOptions[0])),
      0,
      "filmScanGroup"
    );
    const char *scanWorkingColorSpaceOptions[] = {
      "Rec.2020 / Linear",
      "ACEScg / Linear",
      "sRGB/Rec.709 / Linear"
    };
    defineChoice(
      paramSet,
      "scanWorkingColorSpace",
      "Scan Working Space",
      scanWorkingColorSpaceOptions,
      static_cast<int>(sizeof(scanWorkingColorSpaceOptions) / sizeof(scanWorkingColorSpaceOptions[0])),
      0,
      "filmScanGroup"
    );
    const char *scanDensityBasisOptions[] = {"Neutral CMY", "Selected Negative Stock"};
    defineChoice(paramSet, "scanDensityBasis", "Scan Density Basis", scanDensityBasisOptions, 2, 0, "filmScanGroup");
    defineDouble3DRange(paramSet, "scanFilmBaseRgb", "Film Base RGB", 1.0, 1.0, 1.0, 0.001, 4.0, "filmScanGroup");
    defineRGB(paramSet, "scanFilmBaseColorRgb", "Film Base Color", 1.0, 0.78, 0.58, scanPublicGroup);
    defineDouble(paramSet, "scanFilmBaseTemp", "Film Base Temp", 0.0, -100.0, 100.0, scanPublicGroup);
    defineDouble(paramSet, "scanFilmBaseTint", "Film Base Tint", 0.0, -100.0, 100.0, scanPublicGroup);
    defineDouble3DRange(paramSet, "scanBlackFlareRgb", "Black / Flare RGB", 0.0, 0.0, 0.0, 0.0, 0.25, "filmScanGroup");
    defineDouble(paramSet, "scanExposureEv", "Scan Exposure EV", 0.0, -8.0, 8.0, scanPublicGroup);
    defineDouble(paramSet, "scanDensityContrast", "Scan Density Contrast", 1.5, 0.25, 4.0, "filmScanGroup");
    defineDouble3DRange(paramSet, "scanDensityScaleRgb", "Density Scale RGB", 1.0, 1.0, 1.0, -4.0, 4.0, "filmScanGroup");
    defineDouble(paramSet, "scanDensityScaleR", "Density Scale R", 1.0, -4.0, 4.0, scanPublicGroup);
    defineDouble(paramSet, "scanDensityScaleG", "Density Scale G", 1.0, -4.0, 4.0, scanPublicGroup);
    defineDouble(paramSet, "scanDensityScaleB", "Density Scale B", 1.0, -4.0, 4.0, scanPublicGroup);
    defineDouble3DRange(paramSet, "scanDensityOffsetRgb", "Density Offset RGB", 0.0, 0.0, 0.0, -2.0, 2.0, "filmScanGroup");
  }
  defineDouble(paramSet, "printPushPullStops", "Print Push / Pull Stops", 0.0, -2.0, 2.0, isProProductionBuild() ? "productionLaboratoryGroup" : "printGroup");

  defineDouble(paramSet, "filmExposureEv", "Exposure EV", 0.0, -3.0, 3.0, isProProductionBuild() ? "productionCameraGroup" : "filmGroup");
  defineBool(paramSet, "autoExposure", "Auto Exposure", false, "filmGroup");
  const char *autoExposureMethods[] = {"Center weighted", "Median"};
  defineChoice(paramSet, "autoExposureMethod", "Auto Exposure Meter", autoExposureMethods, 2, 0, "filmGroup");
  defineDouble(paramSet, "filmGamma", "Gamma", 1.0, 0.1, 2.0, "filmGroup");
  defineDouble(paramSet, "printExposureEv", "Exposure EV", 0.0, -5.0, 5.0, "printGroup");
  defineDouble(paramSet, "printGamma", "Gamma", 1.0, 0.1, 2.0, "printGroup");
  defineDouble(paramSet, "printShadowShape", "Shadow Shape", 0.0, -1.0, 1.0, "printGroup");
  defineDouble(paramSet, "printHighlightShape", "Highlight Shape", 0.0, -1.0, 1.0, "printGroup");
  defineDouble(paramSet, "filterC", "C Filter", 0.0, 0.0, 120.0, "printGroup");
  defineDouble(paramSet, "filterMShift", "M Filter Shift", 0.0, -60.0, 60.0, "printGroup");
  defineDouble(paramSet, "filterYShift", "Y Filter Shift", 0.0, -60.0, 60.0, "printGroup");
  defineDouble(paramSet, "enlargerScale", "Scale", 1.0, 1.0, 32.0, "enlargerGroup");
  defineDouble(paramSet, "enlargerOffsetXPercent", "Offset X %", 0.0, -100.0, 100.0, "enlargerGroup");
  defineDouble(paramSet, "enlargerOffsetYPercent", "Offset Y %", 0.0, -100.0, 100.0, "enlargerGroup");
  defineDouble(paramSet, "preflashExposure", "Preflash Exposure", 0.0, 0.0, 1.0, "printGroup");
  defineDouble(paramSet, "preflashMFilterShift", "Preflash M Filter Shift", 0.0, -60.0, 60.0, "printGroup");
  defineDouble(paramSet, "preflashYFilterShift", "Preflash Y Filter Shift", 0.0, -60.0, 60.0, "printGroup");
  defineBool(paramSet, "productionPrinterLightsEnabled", "Enabled Printer Light", true, "productionLaboratoryGroup");
  defineBool(paramSet, "productionPrinterLightsLinked", "Linked Printer Light", false, "productionLaboratoryGroup");
  defineDouble(paramSet, "creativePrinterLightR", "Red Printer Light", 0.0, -24.0, 24.0, "productionLaboratoryGroup");
  defineDouble(paramSet, "creativePrinterLightG", "Green Printer Light", 0.0, -24.0, 24.0, "productionLaboratoryGroup");
  defineDouble(paramSet, "creativePrinterLightB", "Blue Printer Light", 0.0, -24.0, 24.0, "productionLaboratoryGroup");
  defineDouble(paramSet, "printBleachBypassAmount", "Print Bleach Bypass", 0.0, 0.0, 1.0, isProProductionBuild() ? "productionLaboratoryGroup" : "printGroup");
  if constexpr (spektrafilm::kSpektraAcademyPrinterDensityEnabled) {
    defineBool(paramSet, "printerLightsGang", "Gang Printer Points", false, "printGroup");
    defineBool(paramSet, "printerLightsGroup", "Group Printer Points", false, "printGroup");
    defineDouble(paramSet, "printerLightR", "Printer Point R", 0.0, -24.0, 24.0, "printGroup");
    defineDouble(paramSet, "printerLightG", "Printer Point G", 0.0, -24.0, 24.0, "printGroup");
    defineDouble(paramSet, "printerLightB", "Printer Point B", 0.0, -24.0, 24.0, "printGroup");
    defineBool(paramSet, "printerLightCalibration", "Printer Point Calibration", true, "printGroup");
  }
  defineDouble(paramSet, "dirAmount", "Amount", 0.0, 0.0, 2.0, "couplerGroup");
  defineDouble(paramSet, "dirDiffusionUm", "Diffusion um", 20.0, 0.0, 100.0, "couplerGroup");
  defineDouble(paramSet, "dirDiffusionTailUm", "Tail um", 200.0, 0.0, 1000.0, "couplerGroup");
  defineDouble(paramSet, "dirDiffusionTailWeight", "Tail Weight", 0.06, 0.0, 1.0, "couplerGroup");
  defineDouble(paramSet, "dirInhibitionSameLayer", "Same-Layer Inhibition", 1.0, 0.0, 2.0, "couplerGroup");
  defineDouble(paramSet, "dirInhibitionInterlayer", "Interlayer Inhibition", 1.0, 0.0, 2.0, "couplerGroup");
  defineDouble3DRange(paramSet, "dirGammaSameLayerRgb", "Same-Layer Gamma RGB", 0.336, 0.319, 0.273, 0.0, 1.0, "couplerGroup");
  defineDouble2DRange(paramSet, "dirGammaRToGb", "R -> G/B Gamma", 0.353, 0.302, 0.0, 1.0, "couplerGroup");
  defineDouble2DRange(paramSet, "dirGammaGToRb", "G -> R/B Gamma", 0.154, 0.353, 0.0, 1.0, "couplerGroup");
  defineDouble2DRange(paramSet, "dirGammaBToRg", "B -> R/G Gamma", 0.168, 0.226, 0.0, 1.0, "couplerGroup");
  definePushButton(paramSet, "dirCalibrateToStock", "Calibrate to Stock", "couplerGroup");
  defineHiddenBool(paramSet, "dirUsesStockCalibration", true, isProProductionBuild() ? "productionLaboratoryGroup" : nullptr);
  defineBool(paramSet, "grainEnabled", "Enabled", false, "grainGroup");
  const char *grainModels[] = {"Preview", "Production", "Grain Synthesis"};
  defineChoice(paramSet, "grainModel", "Model", grainModels, grainModelOptionCountForFlavor(), 0, "grainGroup");
  defineDouble(paramSet, "grainAmount", "Amount", 1.0, 0.0, 2.0, "grainGroup");
  defineDouble(paramSet, "grainSaturation", "Saturation", 1.0, 0.0, 1.0, "grainGroup");
  defineBool(paramSet, "grainSublayersEnabled", "Sublayers", true, "grainGroup");
  defineInt(paramSet, "grainSubLayerCount", "Sub Layer Count", 1, 1, 8, "grainGroup");
  defineDouble(paramSet, "grainParticleAreaUm2", "Particle Area um2", 0.1, 0.01, 5.0, "grainGroup");
  defineDouble3D(paramSet, "grainParticleScale", "Particle Scale RGB", 1.2, 1.0, 2.5, "grainGroup");
  defineDouble3D(paramSet, "grainParticleScaleLayers", "Layer Scale", 6.0, 1.0, 0.4, "grainGroup");
  defineDouble3D(paramSet, "grainDensityMin", "Density Min", 0.04, 0.05, 0.06, "grainGroup");
  defineDouble3D(paramSet, "grainUniformity", "Uniformity RGB", 0.99, 0.97, 0.98, "grainGroup");
  defineDouble(paramSet, "grainFinalBlurUm", "Final Grain Blur", 7.17, 0.0, 25.0, "grainGroup");
  defineDouble(paramSet, "grainBlurDyeCloudsUm", "Dye Cloud Blur um", 1.0, 0.0, 10.0, "grainGroup");
  defineDouble2D(paramSet, "grainMicroStructure", "Micro Structure", 0.2, 30.0, "grainGroup");
  defineInt(paramSet, kGrainSeedParamName, "Seed", descriptorGrainSeedDefault(), kGrainSeedMin, kGrainSeedMax, "grainGroup");
  defineBool(paramSet, "grainAnimate", "Animate", true, "grainGroup");
  defineDouble(paramSet, "grainSynthesisSize", "Synthesis Size", 1.0, 0.25, 4.0, "grainGroup");
  defineDouble(paramSet, "grainSynthesisAmount", "Synthesis Amount", 1.0, 0.0, 3.0, "grainGroup");
  defineDouble(paramSet, "grainSynthesisSharpness", "Synthesis Sharpness", 1.0, 0.25, 4.0, "grainGroup");
  defineDouble(paramSet, "grainSynthesisQuality", "Synthesis Quality", 1.0, 0.25, 4.0, "grainGroup");
  defineInt(paramSet, "grainSynthesisSamples", "Samples", 128, 1, 2048, "grainSynthesisGroup");
  defineDouble(paramSet, "grainSynthesisMeanRadiusUm", "Mean Radius um", 0.25, 0.05, 10.0, "grainSynthesisGroup");
  defineDouble(paramSet, "grainSynthesisRadiusStdDevRatio", "Radius StdDev Ratio", 0.0, 0.0, 1.0, "grainSynthesisGroup");
  defineDouble(paramSet, "grainSynthesisObservationSigmaUm", "Observation Aperture Sigma um", 1.0, 0.0, 20.0, "grainSynthesisGroup");
  defineDouble(paramSet, "grainSynthesisCellSizeRatio", "Cell Size Ratio", 1.0, 0.25, 2.0, "grainSynthesisGroup");
  defineDouble(paramSet, "grainSynthesisMaxRadiusQuantile", "Max Radius Quantile", 0.999, 0.95, 0.9999, "grainSynthesisGroup");
  defineDouble(paramSet, "grainSynthesisCoverageEpsilon", "Coverage Epsilon", 0.0001, 0.000001, 0.01, "grainSynthesisGroup");
  defineInt(paramSet, "grainSynthesisMaxGrainsPerCell", "Max Grains Per Cell", 32, 1, 128, "grainSynthesisGroup");
  defineDouble3D(paramSet, "grainSynthesisRadiusScale", "Radius Scale RGB", 1.2, 1.0, 2.5, "grainSynthesisGroup");
  defineDouble3D(paramSet, "grainSynthesisLayerScale", "Layer Scale", 6.0, 1.0, 0.4, "grainSynthesisGroup");
  defineBool(paramSet, "grainSynthesisLayered", "Layered", true, "grainSynthesisGroup");
  defineBool(paramSet, "halationEnabled", "Enabled", false, "halationGroup");
  defineDouble(paramSet, "scatterAmount", "Scatter Amount", 1.0, 0.0, 2.0, "halationGroup");
  defineDouble(paramSet, "scatterScale", "Scatter Scale", 1.0, 0.0, 4.0, "halationGroup");
  defineDouble(paramSet, "halationAmount", "Amount", 1.0, 0.0, 4.0, "halationGroup");
  defineDouble(paramSet, "halationScale", "Scale", 1.0, 0.0, 4.0, "halationGroup");
  defineDouble(paramSet, "halationBoostEv", "Boost EV", 0.0, 0.0, 20.0, "halationGroup");
  defineDouble(paramSet, "halationBoostRange", "Boost Range", 0.3, 0.0, 1.0, "halationGroup");
  defineDouble(paramSet, "halationProtectEv", "Protect EV", 4.0, 0.0, 10.0, "halationGroup");
  defineRGB(paramSet, "halationStrength", "Strength RGB", 0.05, 0.015, 0.0, "halationGroup");
  const char *diffusionFamilies[] = {"Glimmerglass", "Black Pro-Mist", "Pro-Mist", "CineBloom"};
  defineBool(paramSet, "cameraDiffusionEnabled", "Camera Enabled", false, "diffusionGroup");
  defineChoice(paramSet, "cameraDiffusionFamily", "Camera Family", diffusionFamilies, 4, 1, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionStrength", "Camera Strength", 0.5, 0.0, 2.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionSpatialScale", "Camera Spatial Scale", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionHaloWarmth", "Camera Halo Warmth", 0.0, -1.5, 1.5, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionCoreIntensity", "Camera Core Intensity", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionCoreSize", "Camera Core Size", 1.0, 0.1, 4.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionHaloIntensity", "Camera Halo Intensity", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionHaloSize", "Camera Halo Size", 1.0, 0.1, 4.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionBloomIntensity", "Camera Bloom Intensity", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "cameraDiffusionBloomSize", "Camera Bloom Size", 1.0, 0.1, 4.0, "diffusionGroup");
  defineBool(paramSet, "printDiffusionEnabled", "Print Enabled", false, "diffusionGroup");
  defineChoice(paramSet, "printDiffusionFamily", "Print Family", diffusionFamilies, 4, 1, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionStrength", "Print Strength", 0.5, 0.0, 2.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionSpatialScale", "Print Spatial Scale", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionHaloWarmth", "Print Halo Warmth", 0.0, -1.5, 1.5, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionCoreIntensity", "Print Core Intensity", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionCoreSize", "Print Core Size", 1.0, 0.1, 4.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionHaloIntensity", "Print Halo Intensity", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionHaloSize", "Print Halo Size", 1.0, 0.1, 4.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionBloomIntensity", "Print Bloom Intensity", 1.0, 0.0, 4.0, "diffusionGroup");
  defineDouble(paramSet, "printDiffusionBloomSize", "Print Bloom Size", 1.0, 0.1, 4.0, "diffusionGroup");
  defineBool(paramSet, "scannerEnabled", "Enabled", false, "scannerGroup");
  defineBool(paramSet, "scannerWhiteCorrection", "White Correction", false, "scannerGroup");
  defineBool(paramSet, "scannerBlackCorrection", "Black Correction", false, "scannerGroup");
  defineDouble(paramSet, "scannerWhiteLevel", "White Level", 0.98, 0.0, 1.0, "scannerGroup");
  defineDouble(paramSet, "scannerBlackLevel", "Black Level", 0.01, 0.0, 1.0, "scannerGroup");
  defineDouble(paramSet, "glarePercent", "Glare Percent", 0.03, 0.0, 0.2, "scannerGroup");
  defineDouble(paramSet, "glareRoughness", "Glare Roughness", 0.7, 0.0, 4.0, "scannerGroup");
  defineDouble(paramSet, "glareBlur", "Glare Blur", 0.5, 0.0, 32.0, "scannerGroup");
  defineDouble(paramSet, "scannerMtf50LpMm", "MTF50 lp/mm", 60.0, 0.0, 300.0, "scannerGroup");
  defineDouble(paramSet, "scannerUnsharpRadiusUm", "Unsharp Radius um", 5.0, 0.0, 100.0, "scannerGroup");
  defineDouble(paramSet, "scannerUnsharpAmount", "Unsharp Amount", 0.7, 0.0, 4.0, "scannerGroup");
  defineLabel(paramSet, "infoVersion", "Version:", SPEKTRAFILM_PRODUCT_VERSION_STRING, "infoGroup");
  defineLabel(paramSet, "infoCreatedBy", "Created by:", "Aedan Diez", "infoGroup");
  defineLabel(paramSet, "infoBasedOn", "Based on work by:", "Andrea Volpato & Johannes Hanika", "infoGroup");
  defineLabel(paramSet, "supportAuthorAndrea", "Author:", "Andrea Volpato & Johannes Hanika", "supportGroup");
  defineLabel(paramSet, "supportAuthorAedan", "Author:", "Aedan Oskar Otto Diez", "supportGroup");
  defineLabel(paramSet, "supportAuthorMagno", "Author:", "Magno Ciqueira", "supportGroup");
  defineLabel(paramSet, "supportAuthorPH", "Author:", "Paulo Henrique Vicer", "supportGroup");
  definePushButton(paramSet, "supportAboutHelp", "About and Help", "supportGroup");
  definePushButton(paramSet, "supportOpenMCNexus", "App MCNexus", "supportGroup");
  defineLabel(
    paramSet,
    "calibrationBuildInfo",
    "Build Mode:",
    isProCalibrationBuild() ? "Pro Calibration" : "Pro Production",
    "calibrationGroup"
  );
  defineLabel(
    paramSet,
    "activeCalibrationInfo",
    "Active Master:",
    activeProductionCalibrationPath().string().c_str(),
    "calibrationGroup"
  );
  defineLabel(paramSet, "hostColourManagementInfo", "Host Colour Management:", "unavailable", "calibrationGroup");
  defineLabel(paramSet, "colourManagementConfigInfo", "Colour Config:", "unavailable", "calibrationGroup");
  defineLabel(paramSet, "ocioConfigInfo", "OCIO Config:", "unavailable", "calibrationGroup");
  defineLabel(paramSet, "sourceColourspaceInfo", "Source Colourspace:", "unavailable", "calibrationGroup");
  defineLabel(paramSet, "outputColourspaceInfo", "Output Colourspace:", "unavailable", "calibrationGroup");
  defineLabel(paramSet, "displayColourspaceInfo", "Display Colourspace:", "unavailable", "calibrationGroup");
  defineLabel(paramSet, "resolvedUserTimelineInfo", "Resolved User Timeline:", "not resolved", "calibrationGroup");
  defineLabel(paramSet, "hostClipPreferencesCallInfo", "GetClipPreferences Calls:", "0", "calibrationGroup");
  defineLabel(paramSet, "hostOutputColourspaceCallInfo", "GetOutputColourspace Calls:", "0", "calibrationGroup");
  defineLabel(paramSet, "hostClipPreferenceRequestInfo", "Last Clip Preference Request:", "not requested", "calibrationGroup");
  defineLabel(paramSet, "hostOutputPreferredRequestInfo", "Host Output Preferred Request:", "not requested", "calibrationGroup");
  defineLabel(paramSet, "hostOutputColourspaceReplyInfo", "Last Output Colourspace Reply:", "not requested", "calibrationGroup");
  const char *hostClipPreferenceModes[] = {"No Preference", "Scene Linear", "Scene Log", "SDR Display", "Raw"};
  defineChoice(paramSet, "hostClipPreferenceMode", "Host Clip Preference", hostClipPreferenceModes, 5, 0, "calibrationGroup");
  const char *hostOutputColourspaceModes[] = {"Reply Default", "Match Source", "Scene Linear", "Raw"};
  defineChoice(paramSet, "hostOutputColourspaceMode", "Host Output Colourspace", hostOutputColourspaceModes, 4, 0, "calibrationGroup");
  definePushButton(paramSet, "refreshHostColourDiagnostics", "Refresh Host Colour Diagnostics", "calibrationGroup");
  definePushButton(paramSet, "saveGlobalCalibration", "Save Global Calibration", "calibrationGroup");
  definePushButton(paramSet, "saveNegativeCalibration", "Save Negative Calibration", "calibrationGroup");
  definePushButton(paramSet, "savePrintCalibration", "Save Print Calibration", "calibrationGroup");
  definePushButton(paramSet, "savePairCalibration", "Save Negative/Print Calibration", "calibrationGroup");
  definePushButton(paramSet, "exportProductionCalibration", "Export Production Calibration", "calibrationGroup");
  definePushButton(paramSet, "loadActiveCalibration", "Load Active Calibration", "calibrationGroup");
  const char *gpuRenderTilingOptions[] = {"Full-frame", "Experimental tiled"};
  defineChoice(paramSet, "gpuRenderTiling", "GPU Render Tiling", gpuRenderTilingOptions, 2, 0, "manageGroup");
  const char *lutSizes[] = {"33", "65"};
  const char *lutDestinations[] = {"User", "DaVinci Resolve", "Nuke", "Adobe Creative", "Final Cut Pro"};
  defineChoice(paramSet, "lutSize", "LUT Size", lutSizes, 2, 1, "manageGroup");
  defineChoice(paramSet, "lutDestination", "LUT Destination", lutDestinations, 5, 0, "manageGroup");
  defineSingleLineString(paramSet, "lutIdentifier", "LUT Identifier", "LookFilmLab", "manageGroup");
  definePushButton(paramSet, "exportLut", "Export LUT", "manageGroup");
  defineSingleLineString(paramSet, "presetName", "Preset Name", "LookFilmLab_preset", "manageGroup");
  const std::vector<PresetEntry> presetEntries = listPresetEntries();
  const std::vector<std::string> presetLabels = presetChoiceLabels(presetEntries);
  std::vector<const char *> presetLabelPointers;
  presetLabelPointers.reserve(presetLabels.size());
  for (const std::string &label : presetLabels) {
    presetLabelPointers.push_back(label.c_str());
  }
  defineChoice(paramSet, "presetSelection", "Preset", presetLabelPointers.data(), static_cast<int>(presetLabelPointers.size()), 0, "manageGroup");
  definePushButton(paramSet, "savePreset", "Save Preset", "manageGroup");
  definePushButton(paramSet, "loadPreset", "Load Preset", "manageGroup");
  definePushButton(paramSet, "copyParams", "Copy Params", "manageGroup");
  definePushButton(paramSet, "pasteParams", "Paste Params", "manageGroup");
  definePushButton(paramSet, "saveDefaults", "Set Defaults", "manageGroup");
  definePushButton(paramSet, "resetDefaults", "Reset Factory Defaults", "manageGroup");

  if (!isProductScan()) {
    PageLayout productionControlsPage = definePage(paramSet, "Controls", "Controls");
    addProductionControlsPageChildren(productionControlsPage);
  }

  gDescribeDefaults = nullptr;
  return kOfxStatOK;
}

OfxStatus describe(OfxImageEffectHandle effect) {
  OfxPropertySetHandle props = nullptr;
  gEffectHost->getPropertySet(effect, &props);
  gPropHost->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
  gPropHost->propSetString(props, kOfxPropShortLabel, 0, kPluginLabel);
  gPropHost->propSetString(props, kOfxPropLongLabel, 0, kPluginLabel);
  gPropHost->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, "MC Plugins");
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthHalf);
  gPropHost->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthFloat);
  gPropHost->propSetString(props, kOfxImageEffectPropColourManagementStyle, 0, kOfxImageEffectColourManagementCore);
  gPropHost->propSetString(props, kOfxImageEffectPropColourManagementAvailableConfigs, 0, kOfxConfigIdentifier);
  gPropHost->propSetString(props, kOfxImageEffectPropClipPreferencesSlaveParam, 0, "hostClipPreferenceMode");
  gPropHost->propSetString(props, kOfxImageEffectPropClipPreferencesSlaveParam, 1, "hostOutputColourspaceMode");
  gPropHost->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
  gPropHost->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
  gPropHost->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 0);
#if defined(__APPLE__) && SPEKTRAFILM_OFX_METAL_GPU_BUFFERS
  gPropHost->propSetString(props, kOfxImageEffectPropMetalRenderSupported, 0, "true");
  gPropHost->propSetString(props, kOfxImageEffectPropCPURenderSupported, 0, "true");
#endif
  return kOfxStatOK;
}

OfxStatus onLoad() {
  if (!gHost) {
    return kOfxStatErrMissingHostFeature;
  }
  gEffectHost = reinterpret_cast<OfxImageEffectSuiteV1 *>(const_cast<void *>(gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1)));
  gPropHost = reinterpret_cast<OfxPropertySuiteV1 *>(const_cast<void *>(gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1)));
  gParamHost = reinterpret_cast<OfxParameterSuiteV1 *>(const_cast<void *>(gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1)));
  gMessageHost = reinterpret_cast<OfxMessageSuiteV1 *>(const_cast<void *>(gHost->fetchSuite(gHost->host, kOfxMessageSuite, 1)));
  if (!gEffectHost || !gPropHost || !gParamHost) {
    return kOfxStatErrMissingHostFeature;
  }
  return kOfxStatOK;
}

OfxStatus pluginMain(const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
  auto effect = reinterpret_cast<OfxImageEffectHandle>(const_cast<void *>(handle));
  if (std::strcmp(action, kOfxActionLoad) == 0) {
    return onLoad();
  }
  if (std::strcmp(action, kOfxActionDescribe) == 0) {
    return describe(effect);
  }
  if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
    return describeInContext(effect, inArgs);
  }
  if (std::strcmp(action, kOfxActionCreateInstance) == 0) {
    return createInstance(effect);
  }
  if (std::strcmp(action, kOfxActionDestroyInstance) == 0) {
    return destroyInstance(effect);
  }
  if (std::strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) {
    if (resetRendererAfterEndSequence()) {
      return releaseInactiveInstanceResources(effect, true);
    }
    if (releaseTransientAfterEndSequence()) {
      return releaseInactiveInstanceResources(effect, false);
    }
    return kOfxStatOK;
  }
  if (std::strcmp(action, kOfxActionPurgeCaches) == 0) {
    return releaseInactiveInstanceResources(effect, resetRendererOnPurgeCaches());
  }
  if (std::strcmp(action, kOfxActionInstanceChanged) == 0) {
    return instanceChanged(effect, inArgs);
  }
  if (std::strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
    return getClipPreferences(effect, outArgs);
  }
  if (std::strcmp(action, kOfxImageEffectActionGetOutputColourspace) == 0) {
    return getOutputColourspace(effect, inArgs, outArgs);
  }
  if (std::strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
    return getRegionOfDefinition(effect, inArgs, outArgs);
  }
  if (std::strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
    return getRegionOfInterest(effect, inArgs, outArgs);
  }
  if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
    return render(effect, inArgs, outArgs);
  }
  return kOfxStatReplyDefault;
}

void setHost(OfxHost *host) {
  gHost = host;
}

OfxPlugin gPlugin = {
  kOfxImageEffectPluginApi,
  1,
  kPluginIdentifier,
  kPluginVersionMajor,
  kPluginVersionMinor,
  setHost,
  pluginMain
};

} // namespace

extern "C" {

SPEKTRA_EXPORT OfxPlugin *OfxGetPlugin(int nth) {
  return nth == 0 ? &gPlugin : nullptr;
}

SPEKTRA_EXPORT int OfxGetNumberOfPlugins(void) {
  return 1;
}

SPEKTRA_EXPORT OfxStatus OfxSetHost(const OfxHost *host) {
  setHost(const_cast<OfxHost *>(host));
  return kOfxStatOK;
}

}
