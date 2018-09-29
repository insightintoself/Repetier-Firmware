#include "../../Repetier.h"

#if ZPROBE_TYPE == 1

uint16_t ZProbeHandler::eprStart;
float ZProbeHandler::height;
float ZProbeHandler::bedDistance;
float ZProbeHandler::coating;
float ZProbeHandler::offsetX;
float ZProbeHandler::offsetY;
float ZProbeHandler::speed;
bool ZProbeHandler::activated;

float ZProbeHandler::getZProbeHeight() {
    return height;
}

void ZProbeHandler::setZProbeHeight(float _height) {
    height = _height;
    EEPROM::markChanged();
}

void ZProbeHandler::activate() {
    if (activated) {
        return;
    }
    // Ensure x and y positions are valid
    if (!Motion1::isAxisHomed(X_AXIS) || !Motion1::isAxisHomed(Y_AXIS)) {
        Motion1::homeAxes((Motion1::isAxisHomed(X_AXIS) ? 0 : 1) + (Motion1::isAxisHomed(Y_AXIS) ? 0 : 2));
    }
    float cPos[NUM_AXES];
    Motion1::copyCurrentOfficial(cPos);
    PrinterType::closestAllowedPositionWithNewXYOffset(cPos, offsetX, offsetY, ZPROBE_BORDER);
    GCode::executeFString(Com::tZProbeStartScript);
    Motion1::moveByOfficial(cPos, XY_SPEED, false);
#if EXTRUDER_IS_Z_PROBE == 0
    Motion1::setToolOffset(-offsetX, -offsetY, 0);
#endif
    activated = true;
}

void ZProbeHandler::deactivate() {
    if (!activated) {
        return;
    }
    float cPos[NUM_AXES];
    Motion1::copyCurrentOfficial(cPos);
    GCode::executeFString(Com::tZProbeEndScript);
    Tool* tool = Tool::getActiveTool();
    Motion1::setToolOffset(-tool->getOffsetX(), -tool->getOffsetY(), -tool->getOffsetZ());
    Motion1::zprobeZOffset = 0;
    Motion1::moveByOfficial(cPos, XY_SPEED, false);
}

float ZProbeHandler::runProbe() {
    float zCorr = 0;
    if (ZProbe->update()) {
        Com::printErrorFLN(PSTR("z-probe triggered before starting probing."));
        return ILLEGAL_Z_PROBE;
    }
    bool wasActivated = activated;
    if (!activated) {
        activate();
    }
    bool alActive = Motion1::isAutolevelActive();
    Motion1::setAutolevelActive(false);
    EndstopMode oldMode = Motion1::endstopMode;
    Motion1::endstopMode = EndstopMode::PROBING;
    Motion1::waitForEndOfMoves(); // defined starting condition
    Motion1::stopMask = 4;        // z trigger is finished
    float cPos[NUM_AXES], tPos[NUM_AXES];
    int32_t cPosSteps[NUM_AXES], tPosSteps[NUM_AXES], corSteps[NUM_AXES];
    Motion1::copyCurrentPrinter(cPos);
    PrinterType::transform(cPos, cPosSteps);
    Motion1::copyCurrentPrinter(tPos);
    float secureDistance = (Motion1::maxPos[Z_AXIS] - Motion1::minPos[Z_AXIS]) * 1.5f;
    tPos[Z_AXIS] -= secureDistance;
    PrinterType::transform(tPos, tPosSteps);
    int32_t secureSteps = lround(secureDistance * Motion1::resolution[Z_AXIS]);
    Motion1::endstopMode = EndstopMode::DISABLED;
#if defined(Z_PROBE_DELAY) && Z_PROBE_DELAY > 0
    HAL::delayMilliseconds(Z_PROBE_DELAY);
#endif
    Motion1::moveByPrinter(tPos, speed, false);
    Motion1::waitForEndOfMoves();
    float z = secureDistance * ((fabsf(tPosSteps[Z_AXIS] - cPosSteps[Z_AXIS]) - Motion1::stepsRemaining[Z_AXIS]) / fabsf(tPosSteps[Z_AXIS] - cPosSteps[Z_AXIS]));
    FOR_ALL_AXES(i) {
        int32_t df = cPosSteps[i] - tPosSteps[i];
        corSteps[i] = ((df > 0) ? 1 : ((df < 0) ? -1 : 0)) * (labs(df) - Motion1::stepsRemaining[i]);
    }
    float zr = 0.0f;
#if Z_PROBE_REPETITIONS > 1
    // We are now at z=0 do repetitions and assume correct them 100%
    float cPos2[NUM_AXES], tPos2[NUM_AXES], tPos3[NUM_AXES];
    FOR_ALL_AXES(i) {
        cPos2[i] = cPos[i];
        tPos2[i] = cPos[i];
        tPos3[i] = cPos[i];
    }
    // Go up to start position for repeated tests
    cPos2[Z_AXIS] = 0;
    FOR_ALL_AXES(i) {
        Motion1::currentPositionTransformed[i] = cPos2[i];
    }
    Motion1::updatePositionsFromCurrentTransformed();
    Motion2::setMotorPositionFromTransformed();
    Motion1::endstopMode = EndstopMode::DISABLED;
    tPos2[Z_AXIS] = Z_PROBE_SWITCHING_DISTANCE;
    tPos3[Z_AXIS] = -1.0f;
    int32_t cPosSteps2[NUM_AXES], tPosSteps2[NUM_AXES], tPosSteps3[NUM_AXES], corSteps2[NUM_AXES];
    PrinterType::transform(cPos2, cPosSteps2);
    PrinterType::transform(tPos2, tPosSteps2);
    PrinterType::transform(tPos3, tPosSteps3);
    Motion1::moveByPrinter(tPos2, Z_SPEED, false);
    Motion1::waitForEndOfMoves();
#ifdef Z_PROBE_RUN_AFTER_EVERY_PROBE
    GCode::executeFString(PSTR(Z_PROBE_RUN_AFTER_EVERY_PROBE));
#endif

    for (fast8_t r = 1; r < Z_PROBE_REPETITIONS; r++) {
    // Go down
#if defined(Z_PROBE_DELAY) && Z_PROBE_DELAY > 0
        HAL::delayMilliseconds(Z_PROBE_DELAY);
#endif
        Motion1::endstopMode = EndstopMode::PROBING;
        Motion1::moveByPrinter(tPos3, speed, false);
        Motion1::waitForEndOfMoves();
        zr += z - 1.0f + (Z_PROBE_SWITCHING_DISTANCE + 1.0) * ((fabsf(tPosSteps3[Z_AXIS] - tPosSteps2[Z_AXIS]) - Motion1::stepsRemaining[Z_AXIS]) / fabsf(tPosSteps3[Z_AXIS] - tPosSteps2[Z_AXIS]));
        // Go up to tPos2
        FOR_ALL_AXES(i) {
            int32_t df = tPosSteps2[i] - tPosSteps3[i];
            corSteps2[i] = ((df > 0) ? 1 : ((df < 0) ? -1 : 0)) * (labs(df) - Motion1::stepsRemaining[i]);
        }
        Motion1::moveRelativeBySteps(corSteps2);
        Motion1::waitForEndOfMoves();
        FOR_ALL_AXES(i) {
            Motion1::currentPositionTransformed[i] = tPos2[i];
        }
        Motion1::updatePositionsFromCurrentTransformed();
        Motion2::setMotorPositionFromTransformed();
    }
    z = (z + zr) / static_cast<float>(Z_PROBE_REPETITIONS);
    // Fix last correction
    FOR_ALL_AXES(i) {
        corSteps[i] -= corSteps2[i];
    }
#endif
    z += height;
    z -= coating;
    z -= zCorr;
    /* DEBUG_MSG2_FAST("StartSteps", cPosSteps[Z_AXIS]);
    DEBUG_MSG2_FAST("EndSteps", tPosSteps[Z_AXIS]);
    DEBUG_MSG2_FAST("CorSteps", corSteps[Z_AXIS]);
    DEBUG_MSG2_FAST("RemainingSteps", Motion1::stepsRemaining[Z_AXIS]);
    DEBUG_MSG2_FAST("Z:", z); */

    Motion1::moveRelativeBySteps(corSteps);
    Motion1::waitForEndOfMoves();

    // Restore starting position
    FOR_ALL_AXES(i) {
        Motion1::currentPositionTransformed[i] = cPos[i];
    }
    Motion1::updatePositionsFromCurrentTransformed();
    Motion2::setMotorPositionFromTransformed();
#ifdef Z_PROBE_RUN_AFTER_EVERY_PROBE
    GCode::executeFString(PSTR(Z_PROBE_RUN_AFTER_EVERY_PROBE));
#endif

    Motion1::endstopMode = oldMode;
    Motion1::setAutolevelActive(alActive);
    if (!wasActivated) {
        deactivate();
    }
    if (ZProbe->update()) {
        Com::printErrorFLN(PSTR("z-probe did not untrigger after going back to start position."));
        return ILLEGAL_Z_PROBE;
    }
    Com::printF(Com::tZProbe, z, 3);
    Com::printF(Com::tSpaceXColon, Motion1::currentPosition[X_AXIS]);
#if DISTORTION_CORRECTION
    if (Printer::distortion.isEnabled()) {
        Com::printF(Com::tSpaceYColon, Motion1::currentPosition[Y_AXIS]);
        Com::printFLN(PSTR(" zCorr:"), zCorr, 3);
    } else {
        Com::printFLN(Com::tSpaceYColon, Motion1::currentPosition[Y_AXIS]);
    }
#else
    Com::printFLN(Com::tSpaceYColon, Motion1::currentPosition[Y_AXIS]);
#endif

    return z;
}

bool ZProbeHandler::probingPossible() {
    return true;
}

float ZProbeHandler::xOffset() {
    return offsetX;
}

float ZProbeHandler::yOffset() {
    return offsetX;
}

void ZProbeHandler::init() {
    eepromReset();
    eprStart = EEPROM::reserve(EEPROM_SIGNATURE_Z_PROBE, 1, 20);
    activated = false;
}

void ZProbeHandler::eepromReset() {
    height = Z_PROBE_HEIGHT;
    bedDistance = Z_PROBE_BED_DISTANCE;
    speed = Z_PROBE_SPEED;
    offsetX = Z_PROBE_X_OFFSET;
    offsetY = Z_PROBE_Y_OFFSET;
    coating = Z_PROBE_COATING;
}

void ZProbeHandler::eepromHandle() {
    EEPROM::handlePrefix(PSTR("Z-probe"));
    EEPROM::handleFloat(eprStart + 0, PSTR("trigger height [mm]"), 3, height);
    EEPROM::handleFloat(eprStart + 4, PSTR("min. nozzle distance [mm]"), 3, bedDistance);
    EEPROM::handleFloat(eprStart + 8, PSTR("Probing Speed [mm]"), 3, speed);
    EEPROM::handleFloat(eprStart + 12, PSTR("X offset [mm]"), 3, offsetX);
    EEPROM::handleFloat(eprStart + 16, PSTR("Y offset [mm]"), 3, offsetY);
    EEPROM::removePrefix();
}
#endif