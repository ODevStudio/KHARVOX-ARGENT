const HandCalibration&HandRenderer::Impl::renderCalibration(bool left,
    HandModelKind kind,HandWeaponKind weapon,bool leftHanded)const{
    if(auto value=calibrationDraft.find(poseKey(weapon,leftHanded,left)))return *value;
    if(auto value=calibrationDraft.find(poseKey(HandWeaponKind::Unknown,leftHanded,left)))return *value;
    const auto selection=selectHandCalibrationProfile(left,leftHanded,weapon,
        kind==HandModelKind::GunHolding);
    if(selection.weaponSpecific)
        return weaponCalibrations[selection.weaponIndex][selection.handIndex];
    return left?leftCalibration:rightCalibration;
}

void HandRenderer::Impl::loadPoseCalibrationFile(const wchar_t* filename){
    argent::calibration::readCalibrationFile(root/filename,calibrationDraft.saved,[](auto& row,HandCalibration& value){
        if(!(row>>value.position[0]>>value.position[1]>>value.position[2]>>value.rotationDegrees[0]>>value.rotationDegrees[1]>>value.rotationDegrees[2]>>value.scale))return false;
        bool valid=std::isfinite(value.scale)&&value.scale>0;
        for(float v:value.position)valid=valid&&std::isfinite(v);
        for(float v:value.rotationDegrees)valid=valid&&std::isfinite(v);
        return valid;
    });
}
