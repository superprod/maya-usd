//
// Copyright 2020 Autodesk
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
#ifndef MAYAUSD_USDTRANSFORM3DBASE_H
#define MAYAUSD_USDTRANSFORM3DBASE_H

#include <mayaUsd/base/api.h>
#include <mayaUsd/ufe/UsdTransform3dReadImpl.h>

#include <usdUfe/ufe/UsdSceneItem.h>

#include <pxr/usd/usd/prim.h>

#include <ufe/transform3d.h>
#include <ufe/transform3dHandler.h>

namespace MAYAUSD_NS_DEF {
namespace ufe {

//! \brief Read-only implementation for USD object 3D transform information.
//
// Methods in the interface which return a command to change the object's 3D
// transformation return a null pointer.
//
// Note that all calls to specify time use the default time, but this
// could be changed to use the current time, using getTime(path()).

class MAYAUSD_CORE_PUBLIC UsdTransform3dBase
    : protected UsdTransform3dReadImpl
    , public Ufe::Transform3d
{
public:
    typedef std::shared_ptr<UsdTransform3dBase> Ptr;

    UsdTransform3dBase(const UsdUfe::UsdSceneItem::Ptr& item);

    MAYAUSD_DISALLOW_COPY_MOVE_AND_ASSIGNMENT(UsdTransform3dBase);

    // Ufe::Transform3d overrides
    const Ufe::Path&    path() const override;
    Ufe::SceneItem::Ptr sceneItem() const override;

    inline UsdUfe::UsdSceneItem::Ptr usdSceneItem() const
    {
        return UsdTransform3dReadImpl::usdSceneItem();
    }
    inline PXR_NS::UsdPrim prim() const { return UsdTransform3dReadImpl::prim(); }

    Ufe::TranslateUndoableCommand::Ptr translateCmd(double x, double y, double z) override;
    Ufe::RotateUndoableCommand::Ptr    rotateCmd(double x, double y, double z) override;
    Ufe::ScaleUndoableCommand::Ptr     scaleCmd(double x, double y, double z) override;

    Ufe::TranslateUndoableCommand::Ptr rotatePivotCmd(double x, double y, double z) override;
    Ufe::Vector3d                      rotatePivot() const override;

    Ufe::TranslateUndoableCommand::Ptr scalePivotCmd(double x, double y, double z) override;
    Ufe::Vector3d                      scalePivot() const override;

    Ufe::TranslateUndoableCommand::Ptr
                  translateRotatePivotCmd(double x, double y, double z) override;
    Ufe::Vector3d rotatePivotTranslation() const override;
    Ufe::TranslateUndoableCommand::Ptr
                  translateScalePivotCmd(double x, double y, double z) override;
    Ufe::Vector3d scalePivotTranslation() const override;

    Ufe::SetMatrix4dUndoableCommand::Ptr setMatrixCmd(const Ufe::Matrix4d& m) override;
    Ufe::Matrix4d                        matrix() const override;

    Ufe::Matrix4d segmentInclusiveMatrix() const override;
    Ufe::Matrix4d segmentExclusiveMatrix() const override;

protected:
    // Check if the attribute edit is allowed inside the edit context.
    bool isAttributeEditAllowed(const PXR_NS::TfToken attrName) const;
    bool isAttributeEditAllowed(const PXR_NS::TfToken* attrNames, int count) const;
}; // UsdTransform3dBase

} // namespace ufe
} // namespace MAYAUSD_NS_DEF

#endif // MAYAUSD_USDTRANSFORM3DBASE_H
