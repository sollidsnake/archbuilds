# Builds the Icons-Only Task Manager from plasma-desktop's applets/taskmanager on its own,
# as a separate widget (org.archbuilds.icontasks). prepare() in the PKGBUILD puts the sources here.
cmake_minimum_required(VERSION 3.16)

project(plasma-applet-icontasks-sharedpins)

set(QT_MIN_VERSION "6.11.0")
set(KF6_MIN_VERSION "6.30.0")
set(PLASMA_MIN_VERSION "6.8.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQtDeclareLoggingCategory)
include(ECMQmlModule)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Core DBus Gui Qml Quick Widgets)
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Config CoreAddons GlobalAccel I18n KIO Notifications Service WindowSystem XmlGui)
find_package(Plasma ${PLASMA_MIN_VERSION} REQUIRED)
find_package(PlasmaActivities ${PLASMA_MIN_VERSION} REQUIRED)
find_package(PlasmaActivitiesStats ${PLASMA_MIN_VERSION} REQUIRED)
find_package(LibNotificationManager ${PLASMA_MIN_VERSION} CONFIG REQUIRED)
find_package(KSysGuard CONFIG REQUIRED)

add_definitions(-DQT_NO_CONTEXTLESS_CONNECT)
# The stock widget's translations cover everything but the shared pins strings.
add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_org.kde.plasma.taskmanager\")

plasma_add_applet(org.archbuilds.icontasks
    QML_SOURCES
        qml/AudioStream.qml
        qml/ConfigAppearance.qml
        qml/ConfigBehavior.qml
        qml/config.qml
        qml/ContextMenu.qml
        qml/GroupDialog.qml
        qml/GroupExpanderOverlay.qml
        qml/main.qml
        qml/MouseHandler.qml
        qml/PipeWireThumbnail.qml
        qml/PlayerController.qml
        qml/PulseAudio.qml
        qml/ScrollableTextWrapper.qml
        qml/TaskBadgeOverlay.qml
        qml/TaskList.qml
        qml/TaskProgressOverlay.qml
        qml/Task.qml
        qml/ToolTipDelegate.qml
        qml/ToolTipInstance.qml
        qml/ToolTipWindowMouseArea.qml
        qml/code/LayoutMetrics.js
        qml/code/TaskTools.js
    CPP_SOURCES
        backend.cpp
        launcherssource.cpp
        smartlauncherbackend.cpp
        smartlauncheritem.cpp
    RESOURCES
        main.xml
    GENERATE_APPLET_CLASS
)

kconfig_add_kcfg_files(org.archbuilds.icontasks kactivitymanagerd_plugins_settings.kcfgc)

ecm_qt_declare_logging_category(org.archbuilds.icontasks
    HEADER log_settings.h
    IDENTIFIER TASKMANAGER_DEBUG
    CATEGORY_NAME org.archbuilds.icontasks)

target_link_libraries(org.archbuilds.icontasks PRIVATE
    Qt::Core
    Qt::DBus
    Qt::Qml
    Qt::Quick
    Qt::Widgets
    Plasma::Plasma
    Plasma::Activities
    Plasma::ActivitiesStats
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::GlobalAccel
    KF6::I18n
    KF6::KIOCore
    KF6::KIOGui
    KF6::KIOFileWidgets
    KF6::Notifications
    KF6::XmlGui
    KF6::Service
    KF6::WindowSystem
    KSysGuard::ProcessCore
    PW::LibNotificationManager)
