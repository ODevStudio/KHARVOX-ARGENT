void writeControlsConfig() {
    saveSettings();
    auto assets=root/L"assets";std::filesystem::create_directories(assets);
    const auto path=assets/L"argent_controls.cfg";
    std::string previous;const auto error=argent::input::readConfigurationFile(path,previous);
    if(error!=ERROR_SUCCESS&&error!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Cannot read controls");
    std::istringstream old(previous);
    const auto contents=argent::launcher::updateSettings(old,controlsSettings());
    auto temporary=assets/L"argent_controls.cfg.tmp";
    std::ofstream output(temporary);output<<contents;output.close();
    if(!output||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot save controls");
}
