//  SPDX-License-Identifier: GPL-2.0-only
//
//  ThinkCentre.hpp
//  YogaSMC
//
//  Nuvoton NCT6683D Super-I/O EC driver for ThinkCentre/IdeaCentre desktops.
//  Register interface derived from the Linux nct6683 driver
//  (Copyright (C) 2013 Guenter Roeck <linux@roeck-us.net>, GPL-2.0-or-later)
//  and SMCSuperIO (Copyright (c) 2018 joedm, GPL-3.0).
//
//  Fan control protocol (Linux nct6683 store_pwm):
//    write REQ (0x80) to FAN_CFG_CTRL (0xa01), wait ~1 ms,
//    write target PWM to PWM_WRITE(i) (0xa28+i),
//    write DONE (0x40) to FAN_CFG_CTRL.
//

#ifndef ThinkCentre_hpp
#define ThinkCentre_hpp

#include "YogaVPC.hpp"
#include <VirtualSMCSDK/kern_vsmcapi.hpp>
#include "KeyImplementations.hpp"

#define TC_POLLING_INTERVAL 2000
#define TC_MAX_FAN 4
#define TC_MAX_TEMP_SENSOR 8

class ThinkCentre : public YogaVPC
{
    typedef YogaVPC super;
    OSDeclareDefaultStructors(ThinkCentre)

    /**
     *  Super-I/O configuration ports and keys
     */
    static constexpr const UInt16 SioPorts[2] = {0x2E, 0x4E};
    static constexpr UInt8 SioRegLdSel   = 0x07;
    static constexpr UInt8 SioRegDevId   = 0x20;
    static constexpr UInt8 SioRegEnable  = 0x30;
    static constexpr UInt8 SioRegAddr    = 0x60;
    static constexpr UInt8 SioLdHwm      = 0x0B;
    static constexpr UInt16 SioId6683    = 0xC730;
    static constexpr UInt16 SioId6686    = 0xD440;
    static constexpr UInt16 SioId6687    = 0xD590;
    static constexpr UInt16 SioIdMask    = 0xFFF0;

    /**
     *  EC register window offsets relative to the HWM LDN base
     */
    static constexpr UInt8 EcPageRegOff  = 4;
    static constexpr UInt8 EcIndexRegOff = 5;
    static constexpr UInt8 EcDataRegOff  = 6;

    /**
     *  EC register map (16-bit addresses, high byte selects the page)
     */
    static constexpr UInt16 RegMon        = 0x100; // + 2*i, temp/voltage reading
    static constexpr UInt16 RegFanRpm     = 0x140; // + 2*i, 16-bit RPM
    static constexpr UInt16 RegPwm        = 0x160; // + i, current duty
    static constexpr UInt16 RegPwmWrite   = 0xA28; // + i, target duty
    static constexpr UInt16 RegHwmCfg     = 0x180;
    static constexpr UInt16 RegMonCfg     = 0x1A0; // + i, monitored source
    static constexpr UInt16 RegFaninCfg   = 0x1C0; // + i, tach enable
    static constexpr UInt16 RegFanoutCfg  = 0x1D0; // + i, pwm output enable
    static constexpr UInt16 RegFanMin     = 0x3B8; // + 2*i, 16-bit min RPM
    static constexpr UInt16 RegFanCfgCtrl = 0xA01;
    static constexpr UInt8  FanCfgReq     = 0x80;
    static constexpr UInt8  FanCfgDone    = 0x40;
    static constexpr UInt16 RegCustomerId = 0x602;
    static constexpr UInt16 RegBuildYear  = 0x604;
    static constexpr UInt16 RegVersionHi  = 0x608;

    /**
     *  Monitored source labels < 0x60 are temperatures (Linux nct6683 table)
     */
    static constexpr UInt8 MonSrcVoltageStart = 0x60;

    /**
     *  Emulated ThinkPad-style EC offsets for YogaSMC.app compatibility
     *  (see ThinkFanHelper.swift)
     */
    static constexpr UInt32 EmuFanDuty   = 0x2E; // read/write raw duty 0-255
    static constexpr UInt32 EmuFanLevel  = 0x2F; // read/write level 0-7 / 0x47 / 0x84
    static constexpr UInt32 EmuFanSelect = 0x31; // no-op, single fan systems
    static constexpr UInt32 EmuFanRpm    = 0x84; // read 2 bytes RPM

    /**
     *  Detected chip configuration
     */
    UInt16 sioAddr {0};
    UInt16 ecBase {0};
    UInt16 scannedId[2] {0, 0};   // device id seen at each SioPorts entry
    UInt16 customerId {0};

    /**
     *  Fan configuration from FANIN_CFG/FANOUT_CFG
     *  maps published fan index -> tach index
     */
    UInt8 tachIndex[TC_MAX_FAN] = {0, 0, 0, 0};
    UInt8 fanCount {0};
    bool pwmAvailable {false};

    /**
     *  Temperature sensor configuration from MON_CFG
     *  maps published sensor index -> MON register index
     */
    UInt8 monIndex[TC_MAX_TEMP_SENSOR] = {0};
    UInt8 monSource[TC_MAX_TEMP_SENSOR] = {0};
    UInt8 tempSensorCount {0};

    /**
     *  Manual mode state
     */
    bool manualMode {false};
    UInt8 manualLevel {4};
    UInt8 manualDuty {0};
    UInt8 savedHwmCfg {0};

    /**
     *  Set while a REQ/DONE fan configuration sequence is in progress,
     *  readers skip one polling cycle instead of interleaving register access
     */
    _Atomic(uint32_t) handshakeActive;

    /**
     *  Latest readings, updated by the poller
     */
    _Atomic(uint32_t) fanRpm[TC_MAX_FAN];
    _Atomic(uint32_t) tempSensor[TC_MAX_TEMP_SENSOR];

    IOSimpleLock *ioLock {nullptr};
    IOTimerEventSource *poller {nullptr};

    /**
     *  VirtualSMC service registration notifier
     */
    IONotifier *vsmcNotifier {nullptr};

    /**
     *  Registered plugin instance
     */
    VirtualSMCAPI::Plugin vsmcPlugin {
        xStringify(PRODUCT_NAME),
        parseModuleVersion(xStringify(MODULE_VERSION)),
        VirtualSMCAPI::Version,
    };

    /**
     *  Enter/leave Super-I/O configuration mode and select a logical device
     */
    void sioEnter();
    void sioExit();
    void sioSelect(UInt8 ld);
    UInt8 sioRead(UInt8 reg);
    void sioWrite(UInt8 reg, UInt8 value);

    /**
     *  Paged EC register access
     */
    UInt8 ecRead8(UInt16 reg);
    UInt16 ecRead16(UInt16 reg);
    void ecWrite8(UInt16 reg, UInt8 value);

    /**
     *  Probe ports 0x2e/0x4e for a supported Nuvoton EC and read its HWM base
     *
     *  @return true if a supported chip was found
     */
    bool detectChip();

    /**
     *  Read FANIN_CFG/FANOUT_CFG/MON_CFG and set up sensor tables
     */
    void setupFans();
    void setupTemperatures();

    /**
     *  Ensure hardware monitoring is started (HWM_CFG bit 7)
     */
    void initMonitoring();

    /**
     *  Commit a target duty cycle with the REQ/DONE handshake.
     *  The poller is paused for the sequence to keep register accesses ordered.
     *
     *  @param duty target duty cycle 0-255
     *  @return kIOReturnSuccess on success
     */
    IOReturn setFanDuty(UInt8 duty);

    /**
     *  Attempt to hand fan control back to the EC closed-loop control
     */
    IOReturn restoreAutoMode();

    /**
     *  Poll fan and temperature registers into the atomics
     */
    void updateSensors();

    /**
     *  Add available SMC keys
     */
    void addVSMCKey();

    /**
     *  Translate ThinkPad-style level (0-7, 0x47, 0x8x) to a duty cycle
     */
    static constexpr UInt8 levelToDuty(UInt8 level) {
        if (level & 0x80)
            return 0;
        if (level & 0x40)
            return 0xFF; // full speed
        return static_cast<UInt8>((static_cast<UInt16>(level & 0x07) * 255 + 3) / 7);
    }

protected:
    /**
     *  EC register emulation for the YogaSMC.app user client
     */
    virtual IOReturn method_re1b(UInt32 offset, UInt8 *result) APPLE_KEXT_OVERRIDE;
    virtual IOReturn method_recb(UInt32 offset, UInt32 size, OSData **data) APPLE_KEXT_OVERRIDE;
    virtual IOReturn method_we1b(UInt32 offset, UInt8 value) APPLE_KEXT_OVERRIDE;
    virtual IOReturn readECName(const char* name, UInt32 *result) APPLE_KEXT_OVERRIDE;

    virtual void setPropertiesGated(OSObject* props) APPLE_KEXT_OVERRIDE;

public:
    virtual bool init(OSDictionary *dictionary = nullptr) APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;
    virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void updateAll() APPLE_KEXT_OVERRIDE;
    virtual IOReturn setPowerState(unsigned long powerStateOrdinal, IOService * whatDevice) APPLE_KEXT_OVERRIDE;

    static bool vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier);
};

#endif /* ThinkCentre_hpp */
