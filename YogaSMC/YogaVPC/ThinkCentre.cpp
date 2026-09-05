//  SPDX-License-Identifier: GPL-2.0-only
//
//  ThinkCentre.cpp
//  YogaSMC
//
//  Fan reading and control for Lenovo ThinkCentre/IdeaCentre desktops with
//  Nuvoton NCT6683D/NCT6686D/NCT6687D eSIO, attached to the LPC bridge.
//

#include "ThinkCentre.hpp"
#include <architecture/i386/pio.h>

OSDefineMetaClassAndStructors(ThinkCentre, YogaVPC);

constexpr UInt16 ThinkCentre::SioPorts[2];

/**
 *  Monitoring source names, indices 0x00-0x5f of the Linux nct6683 label
 *  table. Sources from MonSrcVoltageStart (0x60) up are voltages and are not
 *  published, so the table stops there. The index range matters: the PECI/PCH
 *  and DIMM entries above 0x18 are what route a sensor to TCXC and TM0p.
 */
static const char *const monSourceName[] = {
    nullptr, "Local", "Diode 0 (curr)", "Diode 1 (curr)",                // 0x00
    "Diode 2 (curr)", "Diode 0 (volt)", "Diode 1 (volt)", "Diode 2 (volt)", // 0x04
    "Thermistor 14", "Thermistor 15", "Thermistor 16", "Thermistor 0",   // 0x08
    "Thermistor 1", "Thermistor 2", "Thermistor 3", "Thermistor 4",      // 0x0c
    "Thermistor 5", "Thermistor 6", "Thermistor 7", "Thermistor 8",      // 0x10
    "Thermistor 9", "Thermistor 10", "Thermistor 11", "Thermistor 12",   // 0x14
    "Thermistor 13", nullptr, nullptr, nullptr,                          // 0x18
    nullptr, nullptr, nullptr, nullptr,                                  // 0x1c
    "PECI 0.0", "PECI 1.0", "PECI 2.0", "PECI 3.0",                      // 0x20
    "PECI 0.1", "PECI 1.1", "PECI 2.1", "PECI 3.1",                      // 0x24
    "PECI DIMM 0", "PECI DIMM 1", "PECI DIMM 2", "PECI DIMM 3",          // 0x28
    nullptr, nullptr, nullptr, nullptr,                                  // 0x2c
    "PCH CPU", "PCH CHIP", "PCH CHIP CPU MAX", "PCH MCH",                // 0x30
    "PCH DIMM 0", "PCH DIMM 1", "PCH DIMM 2", "PCH DIMM 3",              // 0x34
    "SMBus 0", "SMBus 1", "SMBus 2", "SMBus 3",                          // 0x38
    "SMBus 4", "SMBus 5", "DIMM 0", "DIMM 1",                            // 0x3c
    "DIMM 2", "DIMM 3", "AMD TSI Addr 90h", "AMD TSI Addr 92h",          // 0x40
    "AMD TSI Addr 94h", "AMD TSI Addr 96h", "AMD TSI Addr 98h", "AMD TSI Addr 9ah", // 0x44
    "AMD TSI Addr 9ch", "AMD TSI Addr 9dh", nullptr, nullptr,            // 0x48
    nullptr, nullptr, nullptr, nullptr,                                  // 0x4c
    "Virtual 0", "Virtual 1", "Virtual 2", "Virtual 3",                  // 0x50
    "Virtual 4", "Virtual 5", "Virtual 6", "Virtual 7",                  // 0x54
    nullptr, nullptr, nullptr, nullptr,                                  // 0x58
    nullptr, nullptr, nullptr, nullptr,                                  // 0x5c
};

/**
 *  Fan descriptions for the first tach channels on ThinkCentre Tiny
 */
static const char *const fanDescription[TC_MAX_FAN] = {
    "CPU Fan",
    "System Fan",
    "Aux Fan 0",
    "Aux Fan 1",
};

/**
 *  XNU's outb() takes the port first (architecture/i386/pio.h:
 *  outb(i386_ioport_t port, unsigned char datum)), the opposite of the
 *  Linux outb(value, port) the register sequences below are derived from.
 */
static inline void portWrite(UInt16 port, UInt8 value) {
    ::outb(static_cast<i386_ioport_t>(port), static_cast<unsigned char>(value));
}

static inline UInt8 portRead(UInt16 port) {
    return ::inb(static_cast<i386_ioport_t>(port));
}

void ThinkCentre::sioEnter() {
    portWrite(sioAddr, 0x87);
    portWrite(sioAddr, 0x87);
}

void ThinkCentre::sioExit() {
    portWrite(sioAddr, 0xAA);
    portWrite(sioAddr, 0x02);
    portWrite(sioAddr + 1, 0x02);
}

void ThinkCentre::sioSelect(UInt8 ld) {
    portWrite(sioAddr, SioRegLdSel);
    portWrite(sioAddr + 1, ld);
}

UInt8 ThinkCentre::sioRead(UInt8 reg) {
    portWrite(sioAddr, reg);
    return portRead(sioAddr + 1);
}

void ThinkCentre::sioWrite(UInt8 reg, UInt8 value) {
    portWrite(sioAddr, reg);
    portWrite(sioAddr + 1, value);
}

UInt8 ThinkCentre::ecRead8(UInt16 reg) {
    IOSimpleLockLock(ioLock);
    portWrite(ecBase + EcPageRegOff, 0xFF);   // unlock
    portWrite(ecBase + EcPageRegOff, reg >> 8);
    portWrite(ecBase + EcIndexRegOff, reg & 0xFF);
    UInt8 value = portRead(ecBase + EcDataRegOff);
    IOSimpleLockUnlock(ioLock);
    return value;
}

UInt16 ThinkCentre::ecRead16(UInt16 reg) {
    return static_cast<UInt16>(ecRead8(reg) << 8) | ecRead8(reg + 1);
}

void ThinkCentre::ecWrite8(UInt16 reg, UInt8 value) {
    IOSimpleLockLock(ioLock);
    portWrite(ecBase + EcPageRegOff, 0xFF);   // unlock
    portWrite(ecBase + EcPageRegOff, reg >> 8);
    portWrite(ecBase + EcIndexRegOff, reg & 0xFF);
    portWrite(ecBase + EcDataRegOff, value);
    IOSimpleLockUnlock(ioLock);
}

bool ThinkCentre::detectChip() {
    for (auto port : SioPorts) {
        sioAddr = port;
        sioEnter();
        UInt16 id = static_cast<UInt16>(sioRead(SioRegDevId) << 8) | sioRead(SioRegDevId + 1);
        scannedId[port == SioPorts[0] ? 0 : 1] = id;
        if ((id & SioIdMask) != SioId6683 &&
            (id & SioIdMask) != SioId6686 &&
            (id & SioIdMask) != SioId6687) {
            AlwaysLog("SIO 0x%02x: id 0x%04x, no supported Nuvoton EC", port, id);
            sioExit();
            continue;
        }

        sioSelect(SioLdHwm);
        UInt16 base = static_cast<UInt16>(sioRead(SioRegAddr) << 8) | sioRead(SioRegAddr + 1);
        base &= static_cast<UInt16>(~7);
        if (base < 0x100 || base >= 0xFF00) {
            AlwaysLog("SIO 0x%02x: EC base 0x%04x out of range", port, base);
            sioExit();
            continue;
        }

        UInt8 enable = sioRead(SioRegEnable);
        if (!(enable & 0x01)) {
            AlwaysLog("Forcibly enabling EC access. Data may be unusable.");
            sioWrite(SioRegEnable, enable | 0x01);
        }

        sioExit();
        ecBase = base;
        AlwaysLog("Found Nuvoton EC 0x%04x at SIO 0x%02x, HWM base 0x%04x", id, port, ecBase);
        return true;
    }
    AlwaysLog("No supported Nuvoton EC at 0x2E/0x4E, not attaching");
    return false;
}

void ThinkCentre::setupFans() {
    for (UInt8 i = 0; i < 16 && fanCount < TC_MAX_FAN; i++) {
        UInt8 cfg = ecRead8(RegFaninCfg + i);
        DebugLog("FANIN_CFG[%d] = 0x%02x", i, cfg);
        if (cfg & 0x80) {
            tachIndex[fanCount] = i;
            fanCount++;
        }
    }

    pwmAvailable = false;
    for (UInt8 i = 0; i < 8; i++) {
        UInt8 cfg = ecRead8(RegFanoutCfg + i);
        DebugLog("FANOUT_CFG[%d] = 0x%02x", i, cfg);
        if (cfg & 0x80) {
            pwmIndex = i;
            pwmAvailable = true;
            break;
        }
    }
}

void ThinkCentre::setupTemperatures() {
    for (UInt8 i = 0; i < 32 && tempSensorCount < TC_MAX_TEMP_SENSOR; i++) {
        UInt8 src = ecRead8(RegMonCfg + i) & 0x7F;
        // disabled, reserved, or voltage sources
        if (src == 0 || src >= MonSrcVoltageStart || src >= sizeof(monSourceName)/sizeof(monSourceName[0]))
            continue;
        if (monSourceName[src] == nullptr)
            continue;
        monIndex[tempSensorCount] = i;
        monSource[tempSensorCount] = src;
        tempSensorCount++;
    }
}

void ThinkCentre::initMonitoring() {
    UInt8 cfg = ecRead8(RegHwmCfg);
    if (!(cfg & 0x80))
        ecWrite8(RegHwmCfg, cfg | 0x80);
}

IOReturn ThinkCentre::setFanDuty(UInt8 duty) {
    if (!pwmAvailable)
        return kIOReturnUnsupported;

    // readers skip a cycle while the configuration sequence is in flight
    atomic_store_explicit(&handshakeActive, 1, memory_order_release);

    ecWrite8(RegFanCfgCtrl, FanCfgReq);
    IODelay(1500);
    ecWrite8(RegPwmWrite + pwmIndex, duty);
    ecWrite8(RegFanCfgCtrl, FanCfgDone);

    atomic_store_explicit(&handshakeActive, 0, memory_order_release);

    manualMode = true;
    manualDuty = duty;
    setProperty("FanManualMode", true);
    setProperty("FanDuty", duty, 8);
    AlwaysLog("Fan duty set to 0x%02x", duty);
    return kIOReturnSuccess;
}

IOReturn ThinkCentre::restoreAutoMode() {
    atomic_store_explicit(&handshakeActive, 1, memory_order_release);

    // End any pending configuration; the EC closed-loop control resumes
    ecWrite8(RegFanCfgCtrl, FanCfgReq);
    IODelay(1500);
    ecWrite8(RegFanCfgCtrl, FanCfgDone);

    atomic_store_explicit(&handshakeActive, 0, memory_order_release);

    manualMode = false;
    setProperty("FanManualMode", false);
    AlwaysLog("Fan control returned to auto mode");
    return kIOReturnSuccess;
}

void ThinkCentre::updateSensors() {
    if (atomic_load_explicit(&handshakeActive, memory_order_acquire)) {
        if (poller)
            poller->setTimeoutMS(TC_POLLING_INTERVAL);
        return;
    }

    for (UInt8 i = 0; i < fanCount; i++) {
        UInt16 rpm = ecRead16(RegFanRpm + tachIndex[i] * 2);
        atomic_store_explicit(&fanRpm[i], rpm, memory_order_release);
    }

    for (UInt8 i = 0; i < tempSensorCount; i++) {
        SInt16 raw = static_cast<SInt16>(ecRead16(RegMon + monIndex[i] * 2));
        // 1/256 degree C per LSB, signed, rounded to whole degrees
        SInt32 degC = (static_cast<SInt32>(raw) + 128) >> 8;
        atomic_store_explicit(&tempSensor[i], static_cast<UInt32>(degC), memory_order_release);
    }

    if (!manualMode) {
        UInt8 duty = ecRead8(RegPwm + pwmIndex);
        setProperty("FanDuty", duty, 8);
    }

    if (poller)
        poller->setTimeoutMS(TC_POLLING_INTERVAL);
}

void ThinkCentre::addVSMCKey() {
    VirtualSMCAPI::addKey(KeyFNum, vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(fanCount, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));

    bool cpuKeyPublished = false;

    for (UInt8 i = 0; i < fanCount; i++) {
        atomic_init(&fanRpm[i], 0);
        VirtualSMCAPI::addKey(KeyF0Ac(i), vsmcPlugin.data, VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new atomicFpKey(&fanRpm[i])));

        FanTypeDescStruct fanDesc;
        fanDesc.type = FAN_PWM_TACH;
        strlcpy(fanDesc.strFunction, fanDescription[i], DiagFunctionStrLen);
        VirtualSMCAPI::addKey(KeyF0ID(i), vsmcPlugin.data,
            VirtualSMCAPI::valueWithData(reinterpret_cast<const SMC_DATA *>(&fanDesc), sizeof(fanDesc), SmcKeyTypeFds, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));

        UInt16 minRpm = ecRead16(RegFanMin + tachIndex[i] * 2);
        if (minRpm == 0)
            minRpm = 300;
        VirtualSMCAPI::addKey(KeyF0Mn(i), vsmcPlugin.data, VirtualSMCAPI::valueWithFp(minRpm, SmcKeyTypeFpe2, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
        VirtualSMCAPI::addKey(KeyF0Mx(i), vsmcPlugin.data, VirtualSMCAPI::valueWithFp(5000, SmcKeyTypeFpe2, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
        VirtualSMCAPI::addKey(KeyF0Sf(i), vsmcPlugin.data, VirtualSMCAPI::valueWithFp(800, SmcKeyTypeFpe2, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
    }

    for (UInt8 i = 0; i < tempSensorCount; i++) {
        atomic_init(&tempSensor[i], 0);
        SMC_KEY key;
        // PECI/PCH sources report the CPU package, DIMM sources keep their own slot,
        // everything else lands in a generic slot
        if (!cpuKeyPublished && monSource[i] >= 0x20 && monSource[i] <= 0x37) {
            key = KeyTCXC;
            cpuKeyPublished = true;
        } else if (monSource[i] >= 0x3E && monSource[i] <= 0x41)
            key = KeyTM0p(monSource[i] - 0x3E);
        else
            key = KeyTG0P(i);
        VirtualSMCAPI::addKey(key, vsmcPlugin.data, VirtualSMCAPI::valueWithSp(0, SmcKeyTypeSp78, new atomicSpKey(&tempSensor[i])));
        DebugLog("temperature slot %d: mon %d src 0x%02x (%s)", i, monIndex[i], monSource[i], monSourceName[monSource[i]]);
    }
}

bool ThinkCentre::init(OSDictionary *dictionary) {
    if (!super::init(dictionary))
        return false;

    ioLock = IOSimpleLockAlloc();
    atomic_init(&handshakeActive, 0);
    if (!ioLock)
        AlwaysLog("IOSimpleLockAlloc failed");
    return ioLock != nullptr;
}

void ThinkCentre::free() {
    if (ioLock) {
        IOSimpleLockFree(ioLock);
        ioLock = nullptr;
    }
    super::free();
}

IOService *ThinkCentre::probe(IOService *provider, SInt32 *score) {
    AlwaysLog("Probing %s", provider->getName());
    provider->setProperty("YSMC-TC-Probe", "entered");
    if (!YogaBaseService::probe(provider, score))
        return nullptr;

    iname = "ThinkCentre";

    if (!detectChip()) {
        provider->setProperty("YSMC-TC-Probe", "detectChip-failed");
        provider->setProperty("YSMC-TC-SioId2E", scannedId[0], 32);
        provider->setProperty("YSMC-TC-SioId4E", scannedId[1], 32);
        return nullptr;
    }

    provider->setProperty("YSMC-TC-Probe", "matched");
    provider->setProperty("YSMC-TC-ecBase", ecBase, 32);

    customerId = ecRead16(RegCustomerId);
    AlwaysLog("EC customer id 0x%04x, version %d.%d", customerId, ecRead8(RegVersionHi), ecRead8(RegVersionHi + 1));

    isPMsupported = true;
    setProperty("EC Capability", "RW");
    setProperty("ChipName", "Nuvoton NCT6683D");
    return this;
}

bool ThinkCentre::start(IOService *provider) {
    if (!YogaBaseService::start(provider))
        return false;

    DebugLog("Starting");

    initMonitoring();
    setupFans();
    setupTemperatures();

    if (fanCount == 0)
        AlwaysLog("No fan input enabled, publishing sensors only");

    setProperty("FanCount", fanCount, 8);
    setProperty("PwmAvailable", pwmAvailable);
    setProperty("PwmIndex", pwmIndex, 8);
    setProperty("TachIndex", tachIndex[0], 8);
    setProperty("TempSensorCount", tempSensorCount, 8);

    poller = IOTimerEventSource::timerEventSource(this, [](OSObject *object, IOTimerEventSource *sender) {
        auto me = OSDynamicCast(ThinkCentre, object);
        if (me) me->updateSensors();
    });

    if (!poller || (workLoop->addEventSource(poller) != kIOReturnSuccess)) {
        AlwaysLog("Failed to add poller");
        return false;
    }

    // WARNING: watch out, key addition is sorted here!
    addVSMCKey();
    qsort(const_cast<VirtualSMCKeyValue *>(vsmcPlugin.data.data()), vsmcPlugin.data.size(), sizeof(VirtualSMCKeyValue), VirtualSMCKeyValue::compare);
    setProperty("Key Submitted", vsmcPlugin.data.size(), 32);
    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);

    updateSensors();
    poller->setTimeoutMS(TC_POLLING_INTERVAL);
    poller->enable();
    registerService();
    return true;
}

void ThinkCentre::stop(IOService *provider) {
    DebugLog("Stopping");

    if (manualMode)
        restoreAutoMode();

    if (poller) {
        poller->disable();
        workLoop->removeEventSource(poller);
        OSSafeReleaseNULL(poller);
    }

    if (vsmcNotifier) {
        vsmcNotifier->remove();
        vsmcNotifier = nullptr;
    }

    terminate();
    YogaBaseService::stop(provider);
}

void ThinkCentre::updateAll() {
    updateSensors();
}

bool ThinkCentre::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    auto self = OSDynamicCast(ThinkCentre, reinterpret_cast<OSMetaClassBase*>(sensors));
    if (sensors && vsmc) {
        DBGLOG("thinkcentre", "got vsmc notification");
        auto &plugin = self->vsmcPlugin;
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &plugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            DBGLOG("thinkcentre", "submitted plugin");
            return true;
        } else if (ret != kIOReturnUnsupported) {
            SYSLOG("thinkcentre", "plugin submission failure %X", ret);
        } else {
            DBGLOG("thinkcentre", "plugin submission to non vsmc");
        }
    } else {
        SYSLOG("thinkcentre", "got null vsmc notification");
    }
    return false;
}

IOReturn ThinkCentre::setPowerState(unsigned long powerStateOrdinal, IOService * whatDevice) {
    if (YogaBaseService::setPowerState(powerStateOrdinal, whatDevice) != kIOPMAckImplied)
        return kIOReturnInvalid;

    if (powerStateOrdinal == 0) {
        if (poller) {
            poller->disable();
            workLoop->removeEventSource(poller);
        }
        // the EC loses part of its configuration across sleep
        savedHwmCfg = ecRead8(RegHwmCfg);
        DebugLog("Going to sleep");
    } else {
        ecWrite8(RegHwmCfg, savedHwmCfg);
        initMonitoring();
        if (manualMode)
            setFanDuty(manualDuty);
        if (poller) {
            workLoop->addEventSource(poller);
            poller->setTimeoutMS(TC_POLLING_INTERVAL);
            poller->enable();
        }
        DebugLog("Woke up");
    }
    return kIOPMAckImplied;
}

IOReturn ThinkCentre::method_re1b(UInt32 offset, UInt8 *result) {
    bool busy = atomic_load_explicit(&handshakeActive, memory_order_acquire);
    switch (offset) {
        case EmuFanDuty:
            *result = manualMode ? manualDuty : (busy ? 0 : ecRead8(RegPwm + pwmIndex));
            return kIOReturnSuccess;

        case EmuFanLevel:
            if (manualMode)
                *result = (manualDuty == 0xFF) ? 0x47 : (manualLevel & 0x07);
            else
                *result = 0x84;
            return kIOReturnSuccess;

        case EmuFanSelect:
            *result = 0;
            return kIOReturnSuccess;

        case EmuFanRpm:
        case EmuFanRpm + 1: {
            if (fanCount == 0)
                return kIOReturnUnsupported;
            UInt16 rpm = busy ? static_cast<UInt16>(atomic_load_explicit(&fanRpm[0], memory_order_acquire))
                              : ecRead16(RegFanRpm + tachIndex[0] * 2);
            *result = (offset == EmuFanRpm) ? (rpm & 0xFF) : (rpm >> 8);
            return kIOReturnSuccess;
        }

        default:
            return kIOReturnUnsupported;
    }
}

IOReturn ThinkCentre::method_recb(UInt32 offset, UInt32 size, OSData **data) {
    if (offset == EmuFanRpm && size == 2) {
        if (fanCount == 0)
            return kIOReturnUnsupported;
        UInt16 rpm = atomic_load_explicit(&handshakeActive, memory_order_acquire)
            ? static_cast<UInt16>(atomic_load_explicit(&fanRpm[0], memory_order_acquire))
            : ecRead16(RegFanRpm + tachIndex[0] * 2);
        UInt8 rpmBytes[2] = {static_cast<UInt8>(rpm & 0xFF), static_cast<UInt8>(rpm >> 8)};
        *data = OSData::withBytes(rpmBytes, 2);
        return (*data) ? kIOReturnSuccess : kIOReturnNoMemory;
    }
    return kIOReturnUnsupported;
}

IOReturn ThinkCentre::method_we1b(UInt32 offset, UInt8 value) {
    switch (offset) {
        case EmuFanDuty:
            manualLevel = static_cast<UInt8>((static_cast<UInt16>(value) * 7 + 127) / 255);
            return setFanDuty(value);

        case EmuFanLevel:
            if (value & 0x80) {
                manualLevel = 4;
                return restoreAutoMode();
            }
            if (value & 0x40)
                value = 0x47;
            if ((value & 0x07) > 7)
                return kIOReturnInvalid;
            manualLevel = value & 0x07;
            return setFanDuty(levelToDuty(value));

        case EmuFanSelect:
            // single fan, selection is a no-op
            return kIOReturnSuccess;

        default:
            return kIOReturnUnsupported;
    }
}

IOReturn ThinkCentre::readECName(const char* name, UInt32 *result) {
    // ThinkFanHelper polls the HFSP fan status field
    if (name && strcmp(name, "HFSP") == 0) {
        UInt8 status;
        if (method_re1b(EmuFanLevel, &status) == kIOReturnSuccess) {
            *result = status;
            return kIOReturnSuccess;
        }
    }
    return kIOReturnUnsupported;
}

void ThinkCentre::setPropertiesGated(OSObject* props) {
    OSDictionary* dict = OSDynamicCast(OSDictionary, props);
    if (!dict)
        return;

    OSCollectionIterator* i = OSCollectionIterator::withCollection(dict);

    if (i) {
        while (OSString* key = OSDynamicCast(OSString, i->getNextObject())) {
            if (key->isEqualTo(fanSpeedPrompt)) {
                OSNumber *value;
                getPropertyNumber(fanSpeedPrompt);
                UInt8 speed = value->unsigned8BitValue();
                if (speed > 7 && speed < 0x40)
                    speed = 7;
                if (method_we1b(EmuFanLevel, speed) != kIOReturnSuccess)
                    AlwaysLog("%s update failed", fanSpeedPrompt);
            } else if (key->isEqualTo("FanPWM")) {
                OSNumber *value;
                getPropertyNumber("FanPWM");
                if (setFanDuty(value->unsigned8BitValue()) != kIOReturnSuccess)
                    AlwaysLog("FanPWM update failed");
            } else if (key->isEqualTo("FanAuto")) {
                restoreAutoMode();
            } else if (key->isEqualTo(updatePrompt)) {
                updateSensors();
            } else {
                AlwaysLog("Unknown property %s", key->getCStringNoCopy());
            }
        }
        i->release();
    }

    return;
}
