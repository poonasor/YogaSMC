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
 *  Temperature source names (subset of the Linux nct6683 label table)
 */
static const char *const monSourceName[] = {
    nullptr,        // 0x00 disabled
    "Local",        // 0x01
    "Diode 0",
    "Diode 1",
    "Diode 2",
    nullptr, nullptr, nullptr, // diode voltage variants
    "Thermistor 14",
    "Thermistor 15",
    "Thermistor 16",
    "Thermistor 0",
    "Thermistor 1",
    "Thermistor 2",
    "Thermistor 3",
    "Thermistor 4",
    "Thermistor 5",
    "Thermistor 6",
    "Thermistor 7",
    "Thermistor 8",
    "Thermistor 9",
    "Thermistor 10",
    "Thermistor 11",
    "Thermistor 12",
    "Thermistor 13",
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

void ThinkCentre::sioEnter() {
    ::outb(static_cast<unsigned char>(0x87), static_cast<unsigned short>(sioAddr));
    ::outb(static_cast<unsigned char>(0x87), static_cast<unsigned short>(sioAddr));
}

void ThinkCentre::sioExit() {
    ::outb(static_cast<unsigned char>(0xAA), static_cast<unsigned short>(sioAddr));
    ::outb(static_cast<unsigned char>(0x02), static_cast<unsigned short>(sioAddr));
    ::outb(0x02, sioAddr + 1);
}

void ThinkCentre::sioSelect(UInt8 ld) {
    ::outb(SioRegLdSel, sioAddr);
    ::outb(static_cast<unsigned char>(ld), static_cast<unsigned short>(sioAddr + 1));
}

UInt8 ThinkCentre::sioRead(UInt8 reg) {
    ::outb(static_cast<unsigned char>(reg), static_cast<unsigned short>(sioAddr));
    return ::inb(static_cast<unsigned short>(sioAddr + 1));
}

void ThinkCentre::sioWrite(UInt8 reg, UInt8 value) {
    ::outb(static_cast<unsigned char>(reg), static_cast<unsigned short>(sioAddr));
    ::outb(static_cast<unsigned char>(value), static_cast<unsigned short>(sioAddr + 1));
}

UInt8 ThinkCentre::ecRead8(UInt16 reg) {
    IOSimpleLockLock(ioLock);
    ::outb(static_cast<unsigned char>(0xFF), static_cast<unsigned short>(ecBase + EcPageRegOff));   // unlock
    ::outb(static_cast<unsigned char>(reg >> 8), static_cast<unsigned short>(ecBase + EcPageRegOff));
    ::outb(static_cast<unsigned char>(reg & 0xFF), static_cast<unsigned short>(ecBase + EcIndexRegOff));
    UInt8 value = ::inb(static_cast<unsigned short>(ecBase + EcDataRegOff));
    IOSimpleLockUnlock(ioLock);
    return value;
}

UInt16 ThinkCentre::ecRead16(UInt16 reg) {
    return static_cast<UInt16>(ecRead8(reg) << 8) | ecRead8(reg + 1);
}

void ThinkCentre::ecWrite8(UInt16 reg, UInt8 value) {
    IOSimpleLockLock(ioLock);
    ::outb(static_cast<unsigned char>(0xFF), static_cast<unsigned short>(ecBase + EcPageRegOff));   // unlock
    ::outb(static_cast<unsigned char>(reg >> 8), static_cast<unsigned short>(ecBase + EcPageRegOff));
    ::outb(static_cast<unsigned char>(reg & 0xFF), static_cast<unsigned short>(ecBase + EcIndexRegOff));
    ::outb(static_cast<unsigned char>(value), static_cast<unsigned short>(ecBase + EcDataRegOff));
    IOSimpleLockUnlock(ioLock);
}

bool ThinkCentre::detectChip() {
    for (auto port : SioPorts) {
        sioAddr = port;
        sioEnter();
        UInt16 id = static_cast<UInt16>(sioRead(SioRegDevId) << 8) | sioRead(SioRegDevId + 1);
        if ((id & SioIdMask) != SioId6683 &&
            (id & SioIdMask) != SioId6686 &&
            (id & SioIdMask) != SioId6687) {
            if (id != 0xFFFF)
                DebugLog("unsupported SIO chip id 0x%04x @ 0x%02x", id, port);
            sioExit();
            continue;
        }

        sioSelect(SioLdHwm);
        UInt16 base = static_cast<UInt16>(sioRead(SioRegAddr) << 8) | sioRead(SioRegAddr + 1);
        base &= static_cast<UInt16>(~7);
        if (base == 0) {
            AlwaysLog("EC base I/O port unconfigured");
            sioExit();
            return false;
        }

        UInt8 enable = sioRead(SioRegEnable);
        if (!(enable & 0x01)) {
            AlwaysLog("Forcibly enabling EC access. Data may be unusable.");
            sioWrite(SioRegEnable, enable | 0x01);
        }

        sioExit();
        ecBase = base + EcPageRegOff;
        AlwaysLog("Found Nuvoton EC 0x%04x at SIO 0x%02x, EC base 0x%04x", id, port, ecBase);
        return true;
    }
    return false;
}

void ThinkCentre::setupFans() {
    for (UInt8 i = 0; i < 16 && fanCount < TC_MAX_FAN; i++) {
        if (ecRead8(RegFaninCfg + i) & 0x01) {
            tachIndex[fanCount] = i;
            fanCount++;
        }
    }

    pwmAvailable = false;
    for (UInt8 i = 0; i < 8; i++) {
        if (ecRead8(RegFanoutCfg + i) & 0x80) {
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
    ecWrite8(RegPwmWrite + 0, duty);
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
        UInt16 raw = ecRead16(RegMon + monIndex[i] * 2);
        // 1/256 degree C per LSB
        UInt32 centi = static_cast<UInt32>(raw) * 100 / 256;
        atomic_store_explicit(&tempSensor[i], (centi + 5) / 10, memory_order_release);
    }

    if (!manualMode) {
        UInt8 duty = ecRead8(RegPwm + 0);
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
    if (!YogaBaseService::probe(provider, score))
        return nullptr;

    iname = "ThinkCentre";

    if (!detectChip())
        return nullptr;

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

    if (fanCount == 0) {
        AlwaysLog("No fan input enabled, exiting");
        return false;
    }

    setProperty("FanCount", fanCount, 8);
    setProperty("PwmAvailable", pwmAvailable);

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
            *result = manualMode ? manualDuty : (busy ? 0 : ecRead8(RegPwm + 0));
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
