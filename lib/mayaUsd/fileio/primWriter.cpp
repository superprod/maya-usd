//
// Copyright 2016 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "primWriter.h"

#include <mayaUsd/fileio/jobs/jobArgs.h>
#include <mayaUsd/fileio/translators/translatorGprim.h>
#include <mayaUsd/fileio/utils/adaptor.h>
#include <mayaUsd/fileio/utils/writeUtil.h>
#include <mayaUsd/fileio/writeJobContext.h>
#include <mayaUsd/utils/util.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdUtils/sparseValueWriter.h>

#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

#include <string>
#include <typeinfo>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

PXRUSDMAYA_REGISTER_ADAPTOR_ATTRIBUTE_ALIAS(UsdGeomTokens->purpose, "USD_purpose");

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    (USD_inheritClassNames)
);
// clang-format on

static bool _IsAnimated(const UsdMayaJobExportArgs& args, const MObject& obj)
{
    if (!args.timeSamples.empty()) {
        return UsdMayaUtil::isAnimated(obj);
    }

    return false;
}

UsdMayaPrimWriter::UsdMayaPrimWriter(
    const MFnDependencyNode& depNodeFn,
    const SdfPath&           usdPath,
    UsdMayaWriteJobContext&  jobCtx)
    : _writeJobCtx(jobCtx)
    , _dagPath(UsdMayaUtil::getDagPath(depNodeFn))
    , _mayaObject(depNodeFn.object())
    , _usdPath(usdPath)
    , _baseDagToUsdPaths(UsdMayaUtil::getDagPathMap(depNodeFn, usdPath))
    , _valueWriter(jobCtx.GetArgs().writeDefaults)
    , _exportVisibility(jobCtx.GetArgs().exportVisibility)
    , _hasAnimCurves(_IsAnimated(jobCtx.GetArgs(), depNodeFn.object()))
{
}

/* virtual */
UsdMayaPrimWriter::~UsdMayaPrimWriter() { }

bool UsdMayaPrimWriter::_IsMergedTransform() const
{
    return _writeJobCtx.IsMergedTransform(GetDagPath());
}

bool UsdMayaPrimWriter::_IsMergedShape() const
{
    // For DG nodes, popping an invalid path will silently fail leaving the
    // path invalid, and IsMergedTransform() returns false for invalid paths.
    MDagPath parentPath = GetDagPath();
    parentPath.pop();
    return _writeJobCtx.IsMergedTransform(parentPath);
}

// In the future, we'd like to make this a plugin point.
static bool _GetClassNamesToWrite(const MObject& mObj, std::vector<std::string>* outClassNames)
{
    return UsdMayaWriteUtil::ReadMayaAttribute(
        MFnDependencyNode(mObj), MString(_tokens->USD_inheritClassNames.GetText()), outClassNames);
}

/* virtual */
void UsdMayaPrimWriter::Write(const UsdTimeCode& usdTime)
{
    MStatus                 status;
    const MFnDependencyNode depNodeFn(GetMayaObject(), &status);
    if (status != MS::kSuccess) {
        return;
    }

    // Note that the prim may not actually conform to this schema, so we must
    // check it for validity before using it below.
    UsdGeomImageable imageable(_usdPrim);

    // Visibility is unfortunately special when merging transforms and shapes
    // in that visibility is "pruning" and cannot be overridden by descendants.
    // Thus, we arbitrarily say that when merging transforms and shapes, the
    // _shape_ writer always writes visibility.
    if (imageable && _exportVisibility && !_IsMergedTransform()) {
        bool isVisible = true;
        bool isVisAnimated = false;
        UsdMayaUtil::getPlugValue(depNodeFn, "visibility", &isVisible, &isVisAnimated);

        if (_IsMergedShape()) {
            MDagPath parentDagPath = GetDagPath();
            parentDagPath.pop();
            const MFnDependencyNode parentDepNodeFn(parentDagPath.node());

            bool parentIsVisible = true;
            bool parentIsVisAnimated = false;
            UsdMayaUtil::getPlugValue(
                parentDepNodeFn, "visibility", &parentIsVisible, &parentIsVisAnimated);

            // If BOTH the shape AND the transform are visible, then the
            // prim is visible.
            isVisible = isVisible && parentIsVisible;

            // If the visibility of EITHER the shape OR the transform is
            // animated, then the prim's visibility is animated.
            isVisAnimated = isVisAnimated || parentIsVisAnimated;
        }

        // We write out the current visibility value to the default, regardless
        // if it is animated or not.  If we're not writing to default, we only
        // write visibility if it's animated.
        if (usdTime.IsDefault() || isVisAnimated) {
            const TfToken& visibilityTok
                = (isVisible ? UsdGeomTokens->inherited : UsdGeomTokens->invisible);

            UsdMayaWriteUtil::SetAttribute(
                imageable.CreateVisibilityAttr(VtValue(), true),
                visibilityTok,
                usdTime,
                _GetSparseValueWriter());
        }
    }

    if (usdTime.IsDefault()) {
        // There is no Gprim abstraction in this module, so process the few
        // Gprim attrs here.
        // We imagine that many, but not all, prim writers will write Gprims,
        // so it's OK to skip writing if this isn't a Gprim.
        UsdGeomGprim gprim(_usdPrim);
        if (gprim) {
            GeomSidedness sidedness = GeomSidedness::Derived;
            const TfToken sidednessArg = _writeJobCtx.GetArgs().geomSidedness;
            if (sidednessArg == UsdMayaJobExportArgsTokens->derived) {
                sidedness = GeomSidedness::Derived;
            } else if (sidednessArg == UsdMayaJobExportArgsTokens->single) {
                sidedness = GeomSidedness::Single;
            } else if (sidednessArg == UsdMayaJobExportArgsTokens->double_) {
                sidedness = GeomSidedness::Double;
            } else {
                TF_CODING_ERROR("Unsupported sidedness: %s", sidednessArg.GetString().c_str());
            }

            UsdMayaTranslatorGprim::Write(GetMayaObject(), gprim, nullptr, sidedness);
        }

        // Only write class inherits once at default time.
        std::vector<std::string> classNames;
        if (_GetClassNamesToWrite(GetMayaObject(), &classNames)) {
            UsdMayaWriteUtil::WriteClassInherits(_usdPrim, classNames);
        }

        if (imageable) {
            // Write UsdGeomImageable typed schema attributes.
            // Currently only purpose, which is uniform, so only export at
            // default time.
            UsdMayaWriteUtil::WriteSchemaAttributesToPrim<UsdGeomImageable>(
                GetMayaObject(), _usdPrim, { UsdGeomTokens->purpose }, usdTime, &_valueWriter);
        }

        // Write API schema attributes and strongly-typed metadata.
        // We currently only support these at default time.
        UsdMayaWriteUtil::WriteMetadataToPrim(GetMayaObject(), _usdPrim);
    }

    UsdMayaWriteUtil::WriteAPISchemaAttributesToPrim(
        GetMayaObject(), _usdPrim, _GetExportArgs(), usdTime, _GetSparseValueWriter());

    // Write out user-tagged attributes, which are supported at default time
    // and at animated time-samples.
    UsdMayaWriteUtil::WriteUserExportedAttributes(
        GetMayaObject(), _usdPrim, usdTime, _GetSparseValueWriter());
}

/* virtual */
bool UsdMayaPrimWriter::ExportsGprims() const { return false; }

/* virtual */
bool UsdMayaPrimWriter::ShouldPruneChildren() const { return false; }

/* virtual */
void UsdMayaPrimWriter::PostExport() { MakeSingleSamplesStatic(); }

void UsdMayaPrimWriter::SetExportVisibility(const bool exportVis) { _exportVisibility = exportVis; }

bool UsdMayaPrimWriter::GetExportVisibility() const { return _exportVisibility; }

/* virtual */
const SdfPathVector& UsdMayaPrimWriter::GetModelPaths() const
{
    static const SdfPathVector empty;
    return empty;
}

/* virtual */
const UsdMayaUtil::MDagPathMap<SdfPath>& UsdMayaPrimWriter::GetDagToUsdPathMapping() const
{
    return _baseDagToUsdPaths;
}

const MDagPath& UsdMayaPrimWriter::GetDagPath() const { return _dagPath; }

const MObject& UsdMayaPrimWriter::GetMayaObject() const { return _mayaObject; }

const SdfPath& UsdMayaPrimWriter::GetUsdPath() const { return _usdPath; }

const UsdPrim& UsdMayaPrimWriter::GetUsdPrim() const { return _usdPrim; }

void UsdMayaPrimWriter::_SetUsdPrim(const UsdPrim& usdPrim) { _usdPrim = usdPrim; }

const UsdStageRefPtr& UsdMayaPrimWriter::GetUsdStage() const { return _writeJobCtx.GetUsdStage(); }

const UsdMayaJobExportArgs& UsdMayaPrimWriter::_GetExportArgs() const
{
    return _writeJobCtx.GetArgs();
}

FlexibleSparseValueWriter* UsdMayaPrimWriter::_GetSparseValueWriter() { return &_valueWriter; }

void UsdMayaPrimWriter::MakeSingleSamplesStatic()
{
    auto exportArgs = _GetExportArgs();

    if (!exportArgs.staticSingleSample) {
        return;
    }

    UsdPrim prim = GetUsdPrim();
    if (!prim.IsValid()) {
        return;
    }

    for (auto& attr : prim.GetAttributes()) {
        MakeSingleSamplesStatic(attr);
    }
};

void UsdMayaPrimWriter::MakeSingleSamplesStatic(UsdAttribute attr)
{
    std::vector<double> samples;
    if (attr.GetNumTimeSamples() != 1) {
        return;
    }

    attr.GetTimeSamples(&samples);

    VtValue sample;
    attr.Get(&sample, samples[0]);

    // Clear all time samples and then sets a default
    attr.Clear();
    attr.Set(sample);
}

/* virtual */
bool UsdMayaPrimWriter::_HasAnimCurves() const { return _hasAnimCurves; }

PXR_NAMESPACE_CLOSE_SCOPE
