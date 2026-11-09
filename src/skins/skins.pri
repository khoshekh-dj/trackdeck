unix:SKIN_DEST_DIR = $$PROJECT_DIR/skins
mac:SKIN_DEST_DIR = $$PROJECT_DIR/$${APP_NAME}.app/Contents/MacOS/skins
win32:SKIN_DEST_DIR = $$PROJECT_DIR/Skins
mkpath($$SKIN_DEST_DIR) # qmake builtin: works on every platform/shell

unix:SKINS =  booth khoshekh metro mixer modern retro silver slim turntable
win32:SKINS = Booth Khoshekh Metro Mixer Modern Retro Silver Slim Turntable
for(skin, SKINS) {
    _depends = $$SRC_DIR/skins/$$lower($${skin})
    _target = $$SKIN_DEST_DIR/$${skin}.nzs
    _commands = $$sprintf($$QMAKE_MKDIR_CMD, $$shell_path($$SKIN_DEST_DIR)) && zip -FS -j -X -x\*design.svg $$_target $$_depends/*
    win32:!unix_mingw:_commands = 7z a -tzip -w$$_depends -x\!design.svg $$_target $$_depends/*
    eval($${skin}.depends = $$_depends/*)
    eval($${skin}.target = $$_target)
    eval($${skin}.commands = $$_commands)

    QMAKE_EXTRA_TARGETS += $${skin}
    PRE_TARGETDEPS += $$_target
    system($$_commands)
}

# install
unix:!mac {
    skins.files = ../skins/*
    skins.path = $$PREFIX/share/$$APP_NAME/skins
    INSTALLS += skins
}
