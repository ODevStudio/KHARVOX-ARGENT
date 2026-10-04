bool HandRenderer::Impl::saveCalibration(){
    return calibrationDraft.apply([&](const auto& values){
        const auto target=root/L"hand_pose_calibration_saved.cfg",temporary=root/L"hand_pose_calibration_saved.tmp";
        std::ofstream out(temporary);out<<std::fixed<<std::setprecision(6);
        for(const auto& [key,value]:values){out<<key;
            for(float v:value.position)out<<' '<<v;
            for(float v:value.rotationDegrees)out<<' '<<v;
            out<<' '<<value.scale<<'\n';}
        out.flush();out.close();
        return bool(out)&&MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    });
}
