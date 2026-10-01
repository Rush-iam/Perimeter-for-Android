#include "StdAfx.h"

#include "PrmEdit.h"
#include "Region.h"
#include "CameraManager.h"
#include "Runtime.h"
#include "GameShell.h"
#include "GenericControls.h"
#include "Universe.h"
#include "Config.h"

#include "MissionEdit.h"
#include "UniverseInterface.h"

//#include "InterfaceScript.h"
#include "PerimeterShellUI.h"
#include "Controls.h"
#include "GameContent.h"

#include "Silicon.h"
#include "HistoryScene.h"
#include "BGScene.h"
#include "../Game/MusicManager.h"

#include "RigidBody.h"
#include "chaos.h"
#include "P2P_interface.h"

#include "GenericFilth.h"
#include "GeoControl.h"
#include "AITileMap.h"
#include "PerimeterSound.h"

#include "ExternalShow.h"
#include "Triggers.h"
#include "../TriggerEditor/TriggerEditor.h"

#include "qd_textdb.h"
#include "../HT/ht.h"
#include "VideoPlayer.h"

#include "EditArchive.h"
#include "XPrmArchive.h"
#ifdef __ANDROID__
#include "AndroidTouchInput.h"
#if defined(ANDROID_XR)
#include "AndroidXrBootstrap.h"
#include "AndroidXrListenerPose.h"
#include "xr/XrCameraRig.h"
#include "xr/XrSceneCamera.h"
#include "xr/XrControllerRay.h"
#include "xr/XrControllerLaser.h"
#include "xr/XrUnitRay.h"
#include "DrawBuffer.h"
#include "VertexFormat.h"
#endif
#include <cstring>
#endif
#include "SoundScript.h"
#include "BelligerentSelect.h"
#include "files/files.h"
#include "Localization.h"
#include "codepages/codepages.h"
#include <SDL.h>

#ifdef GPX
#include <c/gamepix.h>
#endif

int terShowFPS = 0;

CShellIconManager   _shellIconManager;
CShellCursorManager _shellCursorManager;

extern char _bMenuMode;
extern char  _bCursorVisible;
extern bool bWasShiftUnpressed;

void SetCameraPosition(cCamera *UCamera,const MatXf &Matrix);
void ToolzerSizeChangeQuant();

void EnterInMissionMenu();
void CancelEditWorkarea();

extern HistoryScene* historyScene;
extern HistoryScene* bwScene;
extern BGScene* bgScene;
extern void PlayMusic(const char *str = 0);

bool terEnableGDIPixel=false;

#if defined(ANDROID_XR)
static Vect3f rotateXrVector(const float orientation[4], const Vect3f& value)
{
    const Vect3f axis(orientation[0], orientation[1], orientation[2]);
    const float dotAxisValue = axis.dot(value);
    const float dotAxisAxis = axis.dot(axis);
    const Vect3f cross(axis.y * value.z - axis.z * value.y,
                       axis.z * value.x - axis.x * value.z,
                       axis.x * value.y - axis.y * value.x);
    const float scalar = orientation[3];
    return axis * (2.0f * dotAxisValue) +
        value * (scalar * scalar - dotAxisAxis) + cross * (2.0f * scalar);
}

static void drawXrControllerLaser(cInterfaceRenderDevice* renderer,
                                  cCamera* camera, float unitsPerMeter,
                                  const AndroidXrEyeView& eye,
                                  const AndroidXrHandState& hand,
                                  float distanceMeters,
                                  const sColor4c& color)
{
    if (!hand.aimValid || distanceMeters <= 0.0f || !eye.width || !eye.height) return;
    const Vect3f direction = rotateXrVector(hand.aimOrientation, Vect3f(0, 0, -1));
    const Vect3f start(hand.aimPosition[0], hand.aimPosition[1], hand.aimPosition[2]);
    const Vect3f finish = start + direction * distanceMeters;
    const float inverseEyeOrientation[4] = {
        -eye.orientation[0], -eye.orientation[1], -eye.orientation[2], eye.orientation[3]};
    const Vect3f eyePosition(eye.position[0], eye.position[1], eye.position[2]);
    Vect3f corners[4];
    if (!xrBuildControllerLaserMesh(
            rotateXrVector(inverseEyeOrientation, start - eyePosition),
            rotateXrVector(inverseEyeOrientation, finish - eyePosition),
            camera->GetZPlane().x / unitsPerMeter, corners)) return;

    renderer->SetNoMaterial(ALPHA_BLEND);
    const uint32_t zwrite = renderer->GetRenderState(RS_ZWRITEENABLE);
    const uint32_t zenable = renderer->GetRenderState(RS_ZENABLE);
    const uint32_t zfunc = renderer->GetRenderState(RS_ZFUNC);
    const uint32_t cullMode = renderer->GetRenderState(RS_CULLMODE);
    renderer->SetDrawTransform(camera);
    renderer->SetWorldMat4f(nullptr);
    renderer->SetRenderState(RS_ZWRITEENABLE, 0);
    renderer->SetRenderState(RS_ZENABLE, 1);
    // DrawScene leaves CMP_ALWAYS set for 2D overlays. Restore a real depth
    // comparison so world geometry can hide the controller laser.
    renderer->SetRenderState(RS_ZFUNC, CMP_LESSEQUAL);
    auto* buffer = renderer->GetDrawBuffer(sVertexXYZDT1::fmt, PT_TRIANGLES);
    auto* vertices = buffer->LockQuad<sVertexXYZDT1>(1);
    const uint32_t diffuse = renderer->ConvertColor(color);
    for (unsigned i = 0; i < 4; ++i) {
        Vect3f world;
        camera->GetMatrix().invXformPoint(corners[i] * unitsPerMeter, world);
        vertices[i].setPos(world);
        vertices[i].diffuse = diffuse;
        vertices[i].u1() = vertices[i].v1() = 0.0f;
    }
    buffer->Unlock();
    renderer->SetRenderState(RS_ZFUNC, zfunc);
    renderer->SetRenderState(RS_ZENABLE, zenable);
    renderer->SetRenderState(RS_ZWRITEENABLE, zwrite);
    renderer->SetRenderState(RS_CULLMODE, cullMode);
}

static void drawXrPanelCursor(cInterfaceRenderDevice* renderer, float pixelX, float pixelY)
{
    const int x = static_cast<int>(pixelX);
    const int y = static_cast<int>(pixelY);
    const sColor4c outline(0, 0, 0, 255);
    const sColor4c cursor(255, 235, 64, 255);
    const auto drawArms = [&](const sColor4c& color, float width) {
        renderer->DrawLine(x - 48, y, x - 12, y, color, width);
        renderer->DrawLine(x + 12, y, x + 48, y, color, width);
        renderer->DrawLine(x, y - 48, x, y - 12, color, width);
        renderer->DrawLine(x, y + 12, x, y + 48, color, width);
    };
    drawArms(outline, 5.0f);
    drawArms(cursor, 3.0f);
    renderer->DrawRectangle(x - 4, y - 4, 8, 8, outline);
    renderer->DrawRectangle(x - 2, y - 2, 4, 4, cursor);
}

static void getXrHeadPosition(const AndroidXrEyeView views[2], float position[3])
{
    for (unsigned axis = 0; axis < 3; ++axis)
        position[axis] = (views[0].position[axis] + views[1].position[axis]) * 0.5f;
}

static void publishXrListenerView(const XrCameraRig& rig, const MatXf& centerWorld,
                                  const AndroidXrEyeView views[2],
                                  const float headPosition[3], bool focused)
{
    if (!focused) {
        androidXrClearListenerView();
        return;
    }
    const auto& left = views[0].orientation;
    const auto& right = views[1].orientation;
    const QuatF leftQuat(left[3], left[0], left[1], left[2]);
    QuatF rightQuat(right[3], right[0], right[1], right[2]);
    if (leftQuat.dot(rightQuat) < 0.0f) rightQuat.negate();
    QuatF headQuat = leftQuat + rightQuat;
    headQuat.normalize();
    const float headOrientation[4] = {headQuat.x(), headQuat.y(),
                                      headQuat.z(), headQuat.s()};
    MatXf listenerView = centerWorld * rig.Pose(headPosition, headOrientation);
    listenerView.invert();
    // SetCameraPosition flips view Y/Z for the renderer. Sound expects the
    // original camera convention, so undo that basis change on the head view.
    MatXf renderToSound = MatXf::ID;
    renderToSound.rot()[1][1] = renderToSound.rot()[2][2] = -1.0f;
    listenerView = renderToSound * listenerView;
    androidXrPublishListenerView(listenerView);
}

static Vect3f getXrScalePivot(const XrCameraRig& rig, const MatXf& centerWorld,
                             const AndroidXrEyeView views[2])
{
    const MatXf leftWorld = centerWorld * rig.Pose(views[0].position,
                                                  views[0].orientation);
    const MatXf rightWorld = centerWorld * rig.Pose(views[1].position,
                                                   views[1].orientation);
    const Vect3f rayStart = (leftWorld.trans() + rightWorld.trans()) * 0.5f;
    const auto screenCenterDirection = [](const MatXf& eyeWorld,
                                          const AndroidXrEyeView& view) {
        const float x = 0.5f * (std::tan(view.fov[0]) + std::tan(view.fov[1]));
        const float y = 0.5f * (std::tan(view.fov[2]) + std::tan(view.fov[3]));
        Vect3f direction = eyeWorld * Vect3f(x, y, 1) - eyeWorld.trans();
        direction.normalize();
        return direction;
    };
    Vect3f rayDirection =
        screenCenterDirection(leftWorld, views[0]) +
        screenCenterDirection(rightWorld, views[1]);
    rayDirection.normalize();
    constexpr float pivotDistanceGameUnits = 500.0f;
    return rayStart + rayDirection * pivotDistanceGameUnits;
}

static float getXrControllerLaserDistance(const AndroidXrHandState& hand,
                                          const XrCameraRig& rig,
                                          const MatXf& centerWorld,
                                          unsigned uiWidth, unsigned uiHeight,
                                          XrWorldRay& worldRay,
                                          bool worldVisible,
                                          const Vect3f& skyCenter = Vect3f::ZERO,
                                          float skyRadius = 0.0f)
{
    if (!hand.aimValid) return 0.0f;
    constexpr float fallbackDistanceMeters = 5.0f;
    float panelX, panelY, panelDistance;
    const bool panelHit = androidXrHitUiPanel(hand, uiWidth, uiHeight,
                                             &panelX, &panelY, &panelDistance);
    if (!worldVisible) return panelHit ? panelDistance : fallbackDistanceMeters;

    const MatXf worldAim = centerWorld * rig.Pose(hand.aimPosition, hand.aimOrientation);
    const Vect3f origin = worldAim.trans();
    Vect3f direction = worldAim.rot() * Vect3f::K;
    direction.normalize();
    float distance = xrRaySphereDistance(origin, direction, skyCenter, skyRadius);
    if (distance <= 0.0f) distance = fallbackDistanceMeters * rig.UnitsPerMeter();
    if (panelHit) distance = std::min(distance, panelDistance * rig.UnitsPerMeter());

    // cChaos draws its ocean at Z=0, extending from -3 to +4 map widths.
    if (std::abs(direction.z) > 1.0e-6f) {
        const float oceanDistance = -origin.z / direction.z;
        const Vect3f ocean = origin + direction * oceanDistance;
        if (oceanDistance > 0.0f && ocean.x >= -3.0f * vMap.H_SIZE &&
            ocean.x <= 4.0f * vMap.H_SIZE && ocean.y >= -3.0f * vMap.V_SIZE &&
            ocean.y <= 4.0f * vMap.V_SIZE)
            distance = std::min(distance, oceanDistance);
    }
    const float terrainDistance = xrRayTerrainDistance(origin, direction,
        vMap.H_SIZE, vMap.V_SIZE, distance, [](int x, int y) {
            return vMap.GetAlt(x, y) / static_cast<float>(1 << VX_FRACTION);
        });
    if (terrainDistance > 0.0f) distance = std::min(distance, terrainDistance);
    worldRay = {origin, direction, distance};
    return distance / rig.UnitsPerMeter();
}

static void getXrControllerLaserDistances(const AndroidXrInputFrame& input,
                                          const XrCameraRig& rig,
                                          const MatXf& centerWorld,
                                          unsigned uiWidth, unsigned uiHeight,
                                          float (&distancesMeters)[2], bool worldVisible,
                                          const Vect3f& skyCenter = Vect3f::ZERO,
                                          float skyRadius = 0.0f)
{
    XrWorldRay worldRays[2];
    for (unsigned hand = 0; hand < 2; ++hand)
        distancesMeters[hand] = getXrControllerLaserDistance(input.hands[hand],
            rig, centerWorld, uiWidth, uiHeight, worldRays[hand], worldVisible, skyCenter, skyRadius);
    if (!worldVisible || (worldRays[0].distance <= 0.0f && worldRays[1].distance <= 0.0f)) return;

    // UnitGrid is maintained by the logic thread. Use the locked player lists
    // once for both controllers and check visible unit models.
    for (terPlayer* player : universe()->Players) {
        CUNITS_LOCK(player);
        for (terUnitBase* unit : player->units()) {
            if (!unit->alive() || !unit->avatar()) continue;
            cObjectNodeRoot* model = unit->avatar()->GetModelPoint();
            if (model)
                xrIntersectUnitRays(*model, worldRays);
        }
    }
    for (unsigned hand = 0; hand < 2; ++hand)
        distancesMeters[hand] = worldRays[hand].distance / rig.UnitsPerMeter();
}

struct XrPanelHit {
    bool valid = false;
    float x = 0.0f;
    float y = 0.0f;
};

static unsigned chooseXrPointerHand(const AndroidXrInputFrame& input,
                                    const XrPanelHit hits[2], int capturedHand)
{
    if (capturedHand >= 0 && capturedHand < 2 && input.hands[capturedHand].aimValid)
        return static_cast<unsigned>(capturedHand);
    for (unsigned hand = 0; hand < 2; ++hand)
        if (hits[hand].valid && (input.hands[hand].pressed & ANDROID_XR_SELECT))
            return hand;
    if (hits[1].valid || (!hits[0].valid && input.hands[1].aimValid)) return 1;
    return 0;
}

static void prepareXrEyeCameras(XrCameraRig& rig, cScene* scene,
                                cCamera* centerCamera, cCamera* eyeCameras[2],
                                const AndroidXrEyeView views[2])
{
    xrPrepareSceneCameras(scene, centerCamera, eyeCameras, rig, views);
    terSetXrUnitInfoViewPosition(
        (eyeCameras[0]->GetPos() + eyeCameras[1]->GetPos()) * 0.5f);
}

template<class DrawEye>
static bool drawXrEyeViews(cInterfaceRenderDevice* renderer,
                           const AndroidXrEyeView views[2], DrawEye drawEye)
{
    for (unsigned eye = 0; eye < 2; ++eye) {
        if (!androidXrBindEye(renderer, eye)) return false;
        renderer->Fill(0, 0, 0);
        renderer->BeginScene();
        drawEye(eye);
        renderer->SetClipRect(0, 0, static_cast<int>(views[eye].width),
                              static_cast<int>(views[eye].height));
        renderer->FlushPrimitive2D();
        renderer->EndScene();
        renderer->Flush();
        renderer->SetClipRect(0, 0, renderer->GetSizeX(), renderer->GetSizeY());
        androidXrUnbindEye(renderer);
    }
    return true;
}

static bool drawXrUiPanel(cInterfaceRenderDevice* renderer,
                          CShellLogicDispatcher* dispatcher,
                          unsigned width, unsigned height,
                          bool cursorVisible, float cursorX, float cursorY)
{
    if (!androidXrBeginUiPanel(renderer, width, height)) return false;
    renderer->Fill(0, 0, 0, 0);
    renderer->BeginScene();
    renderer->SetClipRect(0, 0, static_cast<int>(width), static_cast<int>(height));
    _shellIconManager.draw();
    if (dispatcher) dispatcher->draw();
    if (cursorVisible) {
        androidXrRestoreUiPanelViewport(renderer, width, height);
        drawXrPanelCursor(renderer, cursorX, cursorY);
    }
    renderer->FlushPrimitive2D();
    renderer->EndScene();
    renderer->Flush();
    androidXrEndUiPanel(renderer);
    return true;
}

#endif

//extern XStream quantTimeLog;

int showReels( float, float ) {
	for (int i = 0; i < REEL_COUNT; i++) {
		gameShell->reelAbortEnabled = reels[i].abortEnabled;
		if (reels[i].video) {
			gameShell->showReelModal(reels[i].name, 0, reels[i].localized);
		} else {
			gameShell->showPictureModal(reels[i].name, reels[i].localized, reels[i].time);
		}
        if (!gameShell->GameContinue) {
            break;
        }
	}
	gameShell->reelAbortEnabled = true;
//	_shellIconManager.GetWnd(SQSH_MM_SPLASH4)->Show(1);
//	_shellIconManager.SetModalWnd(SQSH_MM_SPLASH4);

	gameShell->switchToInitialMenu();
	return 0;
}

void abortWithMessage(const std::string& messageID) {
	SNDReleaseSound();
	ErrH.Abort( qdTextDB::instance().getText(messageID.c_str()) );
}

void ErrorInitialize3D() {
    fprintf(stderr, "%dx%d %dhz Display: %d\n", terScreenSizeX, terScreenSizeY, terScreenRefresh, terScreenIndex);
	abortWithMessage("Interface.Menu.Messages.Init3DError");
}

void checkCmdLineArg(const char* argument, const char* argName) {
	if (!argument) {
        std::string msg("Missing cmdline argument ");
        ErrH.Abort(msg + argName);
	}
}

//------------------------
GameShell::GameShell(bool mission_edit) :
chaos(0),
NetClient(0), 
missionEditor_(0),
BuildingInstaller(nullptr),
windowClientSize_(1024, 768)
{
	gameShell = this;

#ifdef PERIMETER_DEBUG
    debugPrm_.load();
#endif

	scriptReelEnabled = false;

	startedWithMainmenu = false;

	setLogicFp();

	setLocalizedFontSizes();

	soundPushedByPause = false;
	soundPushedPushLevel=INT_MIN;

	alwaysRun_ = check_command_line("active");
	GameActive = false;
	GameContinue = true;
	showKeysHelp_ = false;

	autoSwitchAITimer = 0;

	reelManager.sizeType = ReelManager::FULL_SCREEN;

	reelAbortEnabled = true;
	gamePausedByMenu = false;

    IniManager perimeter_ini("Perimeter.ini");
    IniManager perimeter_ini_nocheck("Perimeter.ini", false);

    perimeter_ini_nocheck.getInt("Game","DoubleClickTime", doubleClickTime);
    perimeter_ini_nocheck.getInt("Game","DoubleClickDistance", doubleClickDistance);

	debug_allow_replay = true; //perimeter_ini_nocheck.getInt("Game","EnableReplay");

    terShowFPS = perimeter_ini_nocheck.getInt("Game","ShowFPS");
    check_command_line_parameter("show_fps", terShowFPS);

    MainMenuEnable = perimeter_ini.getInt("Game","MainMenu");
	check_command_line_parameter("mainmenu", MainMenuEnable);
	if(mission_edit)
		MainMenuEnable = false;

	currentSingleProfile.scanProfiles();
	currentSingleProfile.setCurrentProfile(getStringSettings("ProfileName"));
	if (!MainMenuEnable && !currentSingleProfile.isValidProfile()) {
		if (currentSingleProfile.getProfilesVector().empty()) {
			currentSingleProfile.addProfile("Legate");
		}
		currentSingleProfile.setCurrentProfileIndex(0);
	}

	float menuAnimSpeedCoeff = perimeter_ini.getFloat("Graphics","MenuAnimationSpeedFactor");
	_fEffectButtonTime1 *= menuAnimSpeedCoeff;
	_fEffectButtonTime2 *= menuAnimSpeedCoeff;
	_fEffectButtonTime3 *= menuAnimSpeedCoeff;
	_fEffectButtonTotalTime *= menuAnimSpeedCoeff;
	_fEffectIntfRoll1 *= menuAnimSpeedCoeff;
	_fEffectIntfRoll2 *= menuAnimSpeedCoeff;
	_fEffectIntfRoll3 *= menuAnimSpeedCoeff;
	bgEffectTime *= menuAnimSpeedCoeff;


//	autoSwitchAIEnabled = check_command_line("autoSwitchAI") || perimeter_ini.getInt("Game","AutoSwitchAI");
	autoSwitchAIEnabled = check_command_line("autoSwitchAI");

	briefingEnabled = !(disableBriefing || check_command_line("disableBriefing"));

	terCamera->setRestriction(perimeter_ini.getInt("Game","CameraRestriction") && !mission_edit);
	EnableDebugKeyHandlers = EnableDebugKeyHandlersInitial;

	shotNumber_ = -1;
	recordMovie_ = false;
	movieShotNumber_ = 0;

	game_speed_to_resume = game_speed = 1;

	countDownTimeLeft = "";
	countDownTimeMillisLeft = -1;
	countDownTimeMillisLeftVisible = -1;
	
	showWireFrame_ = false;

	initResourceDispatcher();

	cameraMouseTrack = false;
	cameraMouseShift = false;
	cameraMouseZoom  = false;
	toolzerSizeTrack = false;
	
	MouseMoveFlag = 0;
	MousePositionLock = 0;
	
	mouseLeftPressed_ = false;
	mouseRightPressed_ = false;
	mousePosition_ = Vect2f::ZERO;
	mousePositionDelta_ = Vect2f::ZERO;
    mousePositionRelative_ = Vect2f::ZERO;
	
	CursorOverInterface = false;

	activePlayerID_ = 0;
	
	mousePressControl_ = Vect2f::ZERO;
    mapMoveStartCamera_ = terScene->CreateCamera();

	debugFont_ = terVisGeneric->CreateGameFont(sqshFontPopup, 15);

	hotKeyManager = new HotKeyManager();
    hotKeyManager->fillActions();
    hotKeyManager->loadHotKeys();
	_shellCursorManager.Load();

#ifdef PERIMETER_DEBUG
    if(check_command_line("explore")){
        debugPrm_.edit();
        ErrH.Exit();
    }
#endif

	if (check_command_line("pause")) {
		ErrH.Abort("Pause!!!");
	}

    const char* server = check_command_line("server");
    const char* connect_addr = check_command_line("connect");
    const char* connect_room = check_command_line("connect_room");
    if (server || connect_addr || connect_room) {
        CommandLineData data;
        data.server = server != nullptr;
        
        const char* password = check_command_line("password");
        if (password) data.password = password;
        
        const char* playerName = check_command_line("name");
        checkCmdLineArg(playerName, "name");
        data.playerName = playerName;
		
        if (server) {
            checkCmdLineArg(server, "server");
            const char* savePath = check_command_line("save");
            if (savePath) data.save = savePath;
            const char* roomName = check_command_line("room");
            if (roomName) data.roomName = roomName;
            data.address = server;
            const char* publicCmdline = check_command_line("public");
            data.publicHost = publicCmdline != nullptr && std::string(publicCmdline) != "0";
		} else if (connect_room) {
            checkCmdLineArg(connect_room, "connect_room");
            NetRoomID room = strtoull(connect_room, nullptr, 10);
            if (room == 0) {
                ErrH.Abort("Couldn't parse connect_room to number");
            }
            data.roomID = room;
            data.publicHost = true;
            data.addressDefaultPort = NET_RELAY_DEFAULT_PORT;
            if (connect_addr) {
                data.address = connect_addr;
            }
        } else if (connect_addr) {
            checkCmdLineArg(connect_addr, "connect");
            data.address = connect_addr;
            data.publicHost = false;
        }

        startCmdline(data);
        return;
	} else if (MainMenuEnable) {
		startedWithMainmenu = true;
		_shellIconManager.LoadControlsGroup(SHELL_LOAD_GROUP_MENU);
		//_shellIconManager.SwitchMenuScreens(-1, SQSH_MM_SCREEN1);
        
        int splash = perimeter_ini.getInt("Game","StartSplash");
        check_command_line_parameter("start_splash", splash);
        if (splash) {
			_bCursorVisible = 0;
//			_shellIconManager.GetWnd(SQSH_MM_SPLASH1)->Show(1);
//			_shellIconManager.SetModalWnd(SQSH_MM_SPLASH1);
			_shellIconManager.AddDynamicHandler(showReels, CBCODE_QUANT); //ждать пока не слетится
		} else {
            switchToInitialMenu();
		}
	}

	if(mission_edit)
		missionEditor_ = new MissionEditor;

	if(!MainMenuEnable){
	    std::string resource_path = convert_path_content("RESOURCE");
		std::string name;
		std::string path = resource_path;

		if(check_command_line("save")){
			path = convert_path_content(UserSingleProfile::getAllSavesDirectory());
			name = check_command_line("save");
		} else if(check_command_line("mission")){
			path = convert_path_content(MISSIONS_PATH);
			name = check_command_line("mission");
		} else if (check_command_line("open")) {
            name = check_command_line("open");
        } else if (mission_edit) {
			path = convert_path_content(MISSIONS_PATH);
			name = "";
		}

        if (path.empty()) {
            path = resource_path;
        }
		if (name.empty()) {
            name = "XXX";
        }
        
        terminate_with_char(path, PATH_SEP);
		std::string spgPath = convert_path_content(setExtension(path + name, "spg"));
        if (spgPath.empty()) {
            spgPath = setExtension(path + name, "spg");
        }

		if (!XStream(0).open(spgPath)) {
            if (name != "XXX") {
                fprintf(stderr, "File not found: %s\n", spgPath.c_str());
            }
			if (openFileDialog(spgPath, path.c_str(), "spg", "Mission Name")){
				size_t pos = spgPath.rfind(path);
				if (pos != std::string::npos) {
                    spgPath.erase(0, pos);
                }
			} else {
                ErrH.Exit();
            }
        }
		
		if(check_command_line(KEY_REPLAY_REEL)){
			const char* fname=check_command_line(KEY_REPLAY_REEL);
			HTManager::instance()->GameStart(MissionDescription(fname, GT_PLAY_RELL));
		} else {
			int aiMode = 1;
			check_command_line_parameter("AI", aiMode);
            
            if (!XStream(0).open(spgPath)) {
                fprintf(stderr, "File not found: %s\n", spgPath.c_str());
                ErrH.Exit();
            }
            
            MissionDescription md(spgPath.c_str(), aiMode == 1 ? GT_SINGLE_PLAYER : (aiMode == 2 ? GT_SINGLE_PLAYER_ALL_AI : GT_SINGLE_PLAYER_NO_AI));
            if (!md.isCampaign() && string_to_lower(spgPath.c_str()).find("resource" PATH_SEP_STR "missions" PATH_SEP_STR) != std::string::npos) {
                //Workaround to load campaign attrs when using cmdline
                md.missionNumber = 0;
            }
			HTManager::instance()->GameStart(md);
		}

		if(check_command_line("convert")){
			universalSave(spgPath.c_str(), false);
			SNDReleaseSound();
			ErrH.Exit();
		}
	}
}

GameShell::~GameShell()
{
#if defined(ANDROID_XR)
    androidXrClearListenerView();
    delete xrCameraRig_;
#endif
	GameContinue = false;
	setScriptReelEnabled(false);

	if (soundPushedByPause) {
		SNDPausePop();
	}

	HTManager::instance()->GameClose();

	if (missionEditor_) {
        delete missionEditor_;
    }

	done();

	if (NetClient) {
        delete NetClient;
    }

	debugFont_->Release();
	if (hotKeyManager) {
		delete hotKeyManager;
	}
}

void GameShell::done() {
   	historyScene->done();
   	bwScene->done();
   	bgScene->done();
	_shellIconManager.Done();
	_shellCursorManager.Done();
    if (mapMoveStartCamera_) {
        mapMoveStartCamera_->Release();
        mapMoveStartCamera_ = nullptr;
    }
#if defined(ANDROID_XR)
    for (auto& eyeCamera : xrEyeCameras_) {
        if (eyeCamera) eyeCamera->Release();
        eyeCamera = nullptr;
    }
#endif
    if (chaos) {
        delete chaos;
        chaos = nullptr;
    }
}

void GameShell::terminate() {
    GameContinue = false;
    if (reelManager.isVisible()) {
        reelManager.hide();
    }
}

PNetCenter* GameShell::getNetClient() {
//	if (!NetClient) {
//		NetClient = new PNetCenter(true);
//	}
	return NetClient;
}

void GameShell::switchToInitialMenu() {
	_bCursorVisible = 1;
	_WaitCursor();
	_shellIconManager.SwitchMenuScreens(-1, _shellIconManager.initialMenu);
	_shellIconManager.SetModalWnd(0);
}

void GameShell::prepareNetClient() {
    if (NetClient && NetClient->m_state != PNC_STATE__CLIENT_FIND_HOST) {
        destroyNetClient();
    }
    if (!NetClient) {
        NetClient = new PNetCenter();
    }
}

void GameShell::destroyNetClient() {
	if (NetClient) {
		delete NetClient;
		NetClient = nullptr;
	}
}

void GameShell::GameStart(const MissionDescription& mission)
{
    mission.PrintInfo();
    
	_WaitCursor();

	setScriptReelEnabled(false);

	check_determinacy_quant(true);

//	HTManager::instance()->GameClose();
	cVisGeneric::SetAssertEnabled(false);
	CurrentMission = mission;

	for (int i = 0; i < CurrentMission.playersData.size(); i++) {
		PlayerData* data = &CurrentMission.playersData[i];
        std::string playerName;
		if (data->realPlayerType == REAL_PLAYER_TYPE_PLAYER && *(data->name()) == 0) {
            if (currentSingleProfile.isValidProfile() && currentSingleProfile.getLastGameType() != UserSingleProfile::MULTIPLAYER) {
                playerName = currentSingleProfile.getCurrentProfile()->name;
            } else {
                playerName = getBelligerentName(data->belligerent);
            }
		} else if (data->realPlayerType == REAL_PLAYER_TYPE_AI) {
            playerName = getBelligerentName(data->belligerent);
		}
        if (!playerName.empty()) {
            data->setName(playerName);
        }
	}

	LoadProgressStart();

	LoadProgressBlock(0.1f);
	LoadProgressUpdate(1);

	triggersDisabled_ = false;
	cutSceneSkipped_ = false;
	lastSkipTime_.stop();
    
    if (!BuildingInstaller) {
        BuildingInstaller = new terBuildingInstaller();
    }
		
	//vMap.selectUsedWorld(CurrentMission.worldID());

//	SetShadowType(terShadowType,terDrawMeshShadow,false);

	//vMap.fullLoad(perimeter_ini.getInt("TD","FastLoad"));

//	IniManager world_ini(GetTargetName(vMap.worldIniFile));
//	FogStart = world_ini.getFloat("Visualization Parameters","FogStart");
//	FogEnd = world_ini.getFloat("Visualization Parameters","FogEnd");
//	world_ini.getFloatArray("Visualization Parameters","FogColor", 3, &FogColor.r);
//	FogColor /= 255;

    IniManager perimeter_ini("Perimeter.ini");
	setSpeed(perimeter_ini.getFloat("Game", "GameSpeed"));

	terCamera->reset();
#if defined(ANDROID_XR)
    androidXrClearListenerView();
#endif

	LoadProgressBlock(0.6f);
	CurrentMission.packPlayerIDs();
	new terUniverse(NetClient, CurrentMission, LoadProgressUpdate);
    universe()->universalLoad(CurrentMission, this->savePrm(), LoadProgressUpdate);
    
#ifdef PERIMETER_DEBUG
    log_var(logicRND.get());
    /*
    uint32_t attrcrc = startCRC32;
    attrcrc = getSerializationCRC<BinaryOArchive>(rigidBodyPrmLibrary(), attrcrc);
    attrcrc = getSerializationCRC<BinaryOArchive>(attributeLibrary(), attrcrc);
    attrcrc = getSerializationCRC<BinaryOArchive>(globalAttr(), attrcrc);
    log_var(attrcrc);
    */
#endif

    int fogEnable = 1;
    IniManager("Perimeter.ini", false).getInt("Graphics", "FogEnable", fogEnable);
	IniManager world_ini(GetTargetName(vMap.worldIniFile).c_str());
    if (fogEnable) {
        FogStart = world_ini.getFloat("Visualization Parameters", "FogStart");
        FogEnd = world_ini.getFloat("Visualization Parameters", "FogEnd");
    } else {
        FogStart = 0;
        FogEnd = 0;
    }
	world_ini.getFloatArray("Visualization Parameters","FogColor", 3, &FogColor.r);
	FogColor /= 255;

	HTManager::instance()->setSyncroTimer(synchroByClock_, framePeriod_, terMaxTimeInterval);
	HTManager::instance()->setLogicTimePeriod(terLogicTimePeriod);
	HTManager::instance()->setSpeedSyncroTimer(getSpeed());

	LoadProgressBlock(0.7f);
	LoadProgressUpdate(1);
	m_ShellDispatcher.init();
	//universe()->universalLoad(CurrentMission, savePrm());
	createChaos();
	GameActive = true;
	MusicOpenWorld();

//	CameraUpdateFocusPoint();
//	CameraQuant();
//	terCamera->SetPosition(CameraPosition);
//	terCamera->SetTarget(CameraAngle);

	LoadProgressBlock(1);
	LoadProgressUpdate(1);

	bool occlusion=perimeter_ini.getInt("Graphics","EnableOcclusion");
	terVisGeneric->EnableOcclusion(occlusion);

//	float particle_rate=perimeter_ini.getFloat("Graphics","ParticleRate");
//	xassert(particle_rate>=0 && particle_rate<=1);
//	terVisGeneric->SetGlobalParticleRate(particle_rate);

	autoSwitchAITimer = 0;

	_shellIconManager.Done();
	_shellIconManager.LoadControlsGroup(SHELL_LOAD_GROUP_GAME);
	_shellIconManager.load(savePrm().activeTasks);

	terScene->Compact();

	universe()->relaxLoading();
	universe()->setShouldIgnoreIntfCommands(CurrentMission.gameType_ == GT_PLAY_RELL);

	game_speed_to_resume = game_speed;

    if (missionEditor()) {
        setSpeed(0);
    } else if (0 < CurrentMission.gameSpeed) {
		setSpeed(CurrentMission.gameSpeed);
		if(CurrentMission.gamePaused) {
            pauseGame();
        }
	}

	startResourceDispatcher();

	if (NetClient) {
        NetClient->GameIsReady();
    }

	_shellIconManager.gameTypeChanged(currentSingleProfile.getLastGameType());

	_shellIconManager.setCutSceneMode(!(manualData().interfaceEnabled || missionEditor()), false);

	getLogicUpdater().update();
	getLogicUpdater().exchange();

	setCountDownTime(-1);

//	gameTimer_.start();
}

void GameShell::GameClose()
{
#if defined(ANDROID_XR)
    xrZeroplastHand_ = -1;
#endif
	getLogicUpdater().reset();
	stream_interpolator.ClearData();
	m_ShellDispatcher.close();
	//_shellIconManager.Done();

	_pUnitHover = 0;

	if(chaos){
		delete chaos;
		chaos = 0;
	}

	if (universe()) {
        universe()->allSavePlayReel();
        delete universe();
    }

    delete BuildingInstaller;
    BuildingInstaller = nullptr;

	terScene->Compact();

	if (soundPushedByPause) {
		soundPushedByPause = false;
		SNDPausePop();
		xassert(soundPushedPushLevel==SNDGetPushLevel());
		soundPushedPushLevel=INT_MIN;
	}

//	for (int i = SNDGetPushLevel(); i > 0; i--) {
//		SNDPausePop();
//	}
	SNDStopAll();
//	soundPushedByPause = false;
//	soundPushedPushLevel = INT_MIN;

	GameActive = false;
	MusicCloseWorld();
	cVisGeneric::SetAssertEnabled(true);
	debugPrm_.save();

#if 1 && defined(PERIMETER_DEBUG_ASSERT)
    std::vector<cIUnkClass*> allowed;
    allowed.push_back(terLight);
    terScene->CheckPendingObjects(allowed);
#endif
}

bool GameShell::universalSave(const char* name, bool userSave, MissionDescription* missionOutput)
{
	MTAutoSingleThread skip_assert;

    MissionDescription* mission;
    if (missionOutput) {
        *missionOutput = MissionDescription(CurrentMission);
        mission = missionOutput;
    } else {
        mission = new MissionDescription(CurrentMission);
    }
    mission->missionNumber = currentSingleProfile.getCurrentMissionNumber();
    mission->gameContent = mission->isCampaign() ? getGameContentCampaign() : terGameContentSelect;
    if (userSave) {
        mission->globalTime = global_time();
        mission->gameSpeed = game_speed ? game_speed : game_speed_to_resume;
        mission->gamePaused = !gamePausedByMenu && !game_speed;
    } else {
        mission->globalTime = 0;
        mission->gameSpeed = 1;
        mission->gamePaused = false;
    }
    mission->setSaveName(name ? name : "");
    bool result = universe()->universalSave(*mission, userSave);
    if (!mission->savePathKey().empty()) {
        scan_resource_paths(currentSingleProfile.getSavesDirectory());
    }
    if (!missionOutput) {
        //Delete mission since it was temporary
        delete mission;
    }
	return result;
}

void GameShell::NetQuant()
{
	if(NetClient) {
		NetClient->P2PIQuant();
	}
}

bool GameShell::LogicQuant()
{
	start_timer_auto(LogicQuant,STATISTICS_GROUP_TOTAL);
	bool resultTUQuant=true;
	if(GameActive){
		resultTUQuant=universe()->PrimaryQuant();
		getLogicUpdater().update();
		getLogicUpdater().exchange();
	}
	return resultTUQuant;
}


void GameShell::GraphQuant()
{
	if(GameActive){
		universe()->PrepareQuant();

		if(_pShellDispatcher->m_nEditRegion == editRegion1 && isControlPressed() && !isShiftPressed()) {
			toolzerSizeTrack = true;
			ToolzerSizeChangeQuant();
		} else {
			toolzerSizeTrack = false;
			CameraQuant();
		}
	}
//	else {
//		if(NetClient)
//			NetClient->refreshGameHostList();
//	}

	m_ShellDispatcher.quant(GameActive);
	_shellIconManager.quant( frame_time.delta() );
	Show();
	
	mousePositionDelta_ = Vect2f::ZERO;
    mousePositionRelative_ = Vect2f::ZERO;
}

bool GameShell::showEnergy() const 
{
	MTG();
	if(universe()->activePlayer() && universe()->activePlayer()->frame())
	{
		CSELECT_AUTOLOCK();
		const UnitList& select=universe()->select.GetSelectList();
		if (BuildingInstallerInited()) {
            return true;
        } else {
			UnitList::const_iterator ui;
			FOR_EACH(select,ui)
				if((*ui)->attr()->MakeEnergy > 0)
					return true;
		}
	}
	return false;
}

void TestText()
{
	cFont* pFont= terVisGeneric->CreateGameFont(sqshFontPopup, 70);
	cTexture* pTexture=terVisGeneric->CreateTexture("RESOURCE\\Models\\MENU\\Textures\\tv.avi");
	terRenderDevice->SetFont(pFont);

	float time=2.0f;
	float phase= xm::fmod(clockf() * 1e-3, static_cast<double>(time)) / time;

	cTexture *pTexture0,*pTexture1;
	pTexture0 = terVisGeneric->CreateTexture( "RESOURCE\\ICONS\\MAINMENU\\lightmap.tga" );
	pTexture1 = terVisGeneric->CreateTexture( "RESOURCE\\ICONS\\MAINMENU\\tv.avi" ); 
	terRenderDevice->DrawSprite2(600, 400, 128, 128,
		0, 0, 1, 1, 
		0, 0, 1, 1, 
		pTexture0, pTexture1,  1.0f,1.0f, phase );

	pTexture0->Release();
	pTexture1->Release();
/*
	terRenderDevice->OutText(512,320,"HELLO",sColor4f(0,1,0,1),0,ALPHA_BLEND,
		                 pTexture,COLOR_MOD,
						 Vect2f(0,0),
						 Vect2f(1/128.0f,1/128.0f),
						phase,0.5f);

*/
	terRenderDevice->SetFont(NULL);
	pFont->Release();
	pTexture->Release();
}

void GameShell::showWays() {
	int i;
	for (i = 0; i < FRAME_SLOTS_MAX; i++) {
		TerrainButtonData* slotData = &(getLogicUpdater().getLogicData()->slots[i]);
		if (slotData->unit) {
			slotData->unit->showPath(slotData->wayPoints);
		}
	}
	for (i = 0; i < 5; i++) {
		SquadPageData* page = &(getLogicUpdater().getLogicData()->squads[i]);
		if (page->squad) {
			page->squad->showPath(page->wayPoints, page->patrolPoints, page->hasAttackPoint ? (&page->attackPoint) : 0);
		}
	}
	if (getLogicUpdater().getLogicData()->frame) {
		getLogicUpdater().getLogicData()->frame->showPath(getLogicUpdater().getLogicData()->frameWayPoints);
	}
}

void GameShell::renderEndScene() {
    //We need to flush, otherwise the primitive vertex will be after the cursor in the buffer
    terRenderDevice->FlushPrimitive2D();
    terRenderDevice->FlushPrimitive3D();
    
    //Draw FPS
    static FPS fps;
    fps.quant();
    if(terShowFPS){
        float fpsmin = 0.0f;
        float fpsmax = 0.0f;
        fps.GetFPSminmax(fpsmin,fpsmax);
        char s[512];
        char* p=s;
        p+=sprintf(s,"  %s\n", currentVersion);
        p+=sprintf(p,"  FPS=% 3.1f min=% 3.1f max=% 3.1f\n",fps.GetFPS(),fpsmin,fpsmax);

        if (GameActive) {
            float lpsmin, lpsmax;
            HTManager::instance()->GetLogicFPSminmax(lpsmin, lpsmax);
            p += sprintf(p, "  logic=% 2.1f min=% 2.1f\n", HTManager::instance()->GetLogicFps(), lpsmin);
//		    p+=sprintf(p,"  scale time=%i\n",scale_time.delta());
        }

        if(debug_show_mouse_position){
            Vect3f v;
            if(terCamera->cursorTrace(gameShell->mousePosition(),v))
                p+=sprintf(p, "  mouse=(%i,%i,%i)\n", xm::round(v.x), xm::round(v.y), xm::round(v.z));
        }

        xassert(p-s<sizeof(s));
        terRenderDevice->SetFont(_pShellDispatcher->getFont());
        terRenderDevice->OutText(0,16,s,sColor4f(1, 1, 1, 1));
        terRenderDevice->SetFont(nullptr);
    }

    //Draw cursor
    _shellCursorManager.draw();
    //End scene
    terRenderDevice->EndScene();
}

void GameShell::Show()
{
	start_timer_auto(GS_Show,STATISTICS_GROUP_TOTAL);
	frame_time.next_frame();

#if defined(ANDROID_XR)
	if (GameActive) xrMenuPanelPoseAnchored_ = false;
#endif
	if(GameActive){

		if(!isPaused())
			scale_time.next_frame();
		else
			scale_time.skip();

		{
			MetaRegionLock lock(_pShellDispatcher->regionMetaDispatcher());
			_pShellDispatcher->regionMetaDispatcher()->postOperateAnalyze();
		}

		terScene->dSetTime(scale_time.delta());

		terExternalQuant();
		
        if (BuildingInstaller) {
            BuildingInstaller->UpdateInfo(terCamera->GetCamera());
        }
		terScene->PreDraw(terCamera->GetCamera());

		m_ShellDispatcher.PreDraw(frame_time.delta());

#if defined(ANDROID_XR)
        AndroidXrEyeView xrViews[2]{};
        AndroidXrInputFrame xrInput{};
        const auto finishXrBrushStroke = [&](bool cancelTool = false) {
            if (xrZeroplastHand_ < 0) return;
            xrZeroplastHand_ = -1;
            if (cancelTool) {
                CancelEditWorkarea();
            } else {
                MetaRegionLock lock(m_ShellDispatcher.regionMetaDispatcher());
                m_ShellDispatcher.RegionEndEdit();
            }
        };
        if (androidXrBeginFrame(xrViews, &xrInput)) {
            // Menu callbacks can close the mission while this XR frame is open.
            terUniverse* const frameUniverse = universe();
            const auto endXrFrameIfMissionChanged = [&]() {
                if (GameActive && universe() == frameUniverse) return false;
                androidXrEndFrame(false);
                return true;
            };
            cCamera* centerCamera = terCamera->GetCamera();
            if (!xrCameraRig_) xrCameraRig_ = new XrCameraRig();
            xrCameraRig_->SetMenuHeadingAligned(false);
            xrCameraRig_->BeginFrame(xrViews, xrInput.recentered);
            const int scriptedAlignmentMs =
                xrScriptedCameraAlignmentMs_.exchange(-1);
            if (scriptedAlignmentMs >= 0)
                xrCameraRig_->AlignOffsetToScriptedCamera(
                    scriptedAlignmentMs * 0.001f);
            float headPosition[3]{};
            getXrHeadPosition(xrViews, headPosition);
            const float deltaSeconds = frame_time.delta() * 0.001f;
            // XR tabletop navigation is locked during scripted scenes and
            // while the camera is tracking a unit, matching the non-XR view.
            const bool xrTableCameraLocked = isCutSceneMode() ||
                isScriptReelEnabled() || terCamera->unitFollow() ||
                terCamera->xrCameraTransitionActive() ||
                xrCameraRig_->IsAligningToScriptedCamera();
            // Honor runtime tracking recenter above, but preserve the authored
            // camera angles and interpolation points while controls are locked.
            if (xrInput.recentered && !xrTableCameraLocked)
                terCamera->recenterOrientation();
            const bool xrTableControlsEnabled =
                xrInput.focused && !xrTableCameraLocked;
            MatXf centerWorld = centerCamera->GetMatrix();
            centerWorld.invert();
            const Vect3f tablePivot =
                xrTableControlsEnabled && xrCameraRig_->NeedsTablePivot(
                    xrInput.hands[1].thumbstick[1],
                    xrInput.hands[1].thumbstickActive)
                    ? getXrScalePivot(*xrCameraRig_, centerWorld, xrViews)
                    : Vect3f::ZERO;
            // CSkySpere scales its mesh to this radius around (H/2, H/2, 0).
            const float skyRadius = 4.0f * vMap.H_SIZE /
                (vMap.H_SIZE > 2048 ? 2.2f : 1.8f);
            const Vect3f skyCenter(vMap.H_SIZE * 0.5f,
                                   vMap.H_SIZE * 0.5f, 0.0f);
            const Vect3f localPan = xrCameraRig_->UpdateControls(
                xrInput.hands[0].thumbstick,
                xrTableControlsEnabled && xrInput.hands[0].thumbstickActive,
                xrInput.hands[1].thumbstick[1],
                xrTableControlsEnabled && xrInput.hands[1].thumbstickActive,
                deltaSeconds, tablePivot, centerWorld, headPosition,
                skyCenter, skyRadius);
            if (localPan.norm2() > 0.000001f) {
                Vect3f worldPan = centerWorld.rot() * localPan;
                // The rig already pans on the terrain plane. Remove conversion
                // roundoff before rebasing into the game camera.
                worldPan.z = 0.0f;
                const Vect3f appliedWorldPan =
                    terCamera->translateXrPan(worldPan);
                MatXf worldToCenter = centerWorld;
                worldToCenter.invert();
                xrCameraRig_->RebaseCameraPan(
                    worldToCenter.rot() * appliedWorldPan);
                centerWorld = centerCamera->GetMatrix();
                centerWorld.invert();
            }
            publishXrListenerView(*xrCameraRig_, centerWorld, xrViews,
                                  headPosition, xrInput.focused);
            terScene->PrepareViewFamily();
            prepareXrEyeCameras(*xrCameraRig_, terScene, centerCamera,
                                xrEyeCameras_, xrViews);
            const unsigned uiWidth = static_cast<unsigned>(terRenderDevice->GetSizeX());
            const unsigned uiHeight = static_cast<unsigned>(terRenderDevice->GetSizeY());
            androidXrPrepareUiPanel(uiWidth, uiHeight);

            bool uiPointerVisible = false;
            Vect3f xrBrushPosition = Vect3f::ZERO;
            float xrBrushRadius = 0.0f;
            float uiPointerX = 0.0f;
            float uiPointerY = 0.0f;
            bool panelTracked = false;
            if (xrInput.focused) {
                bool panelChanged = false;
                for (unsigned hand = 0; hand < 2; ++hand) {
                    if (xrInput.hands[hand].pressed & ANDROID_XR_PANEL) {
                        if (xrPanelVisible_ && xrPanelHand_ == hand)
                            xrPanelVisible_ = false;
                        else {
                            xrPanelHand_ = hand;
                            xrPanelVisible_ = true;
                        }
                        panelChanged = true;
                    }
                }
                panelTracked = androidXrAttachUiPanelToHand(
                    xrInput.hands[xrPanelHand_], xrPanelHand_);
                if (xrUiPressCaptured_ &&
                    (panelChanged || !xrPanelVisible_ || !panelTracked ||
                     !_shellIconManager.interfaceShowFlag())) {
                    _shellIconManager.lButtonReset();
                    xrUiPressCaptured_ = false;
                    xrUiPressHand_ = -1;
                }
                androidXrSetUiPanelVisible(xrPanelVisible_ && panelTracked &&
                    _shellIconManager.interfaceShowFlag());
                XrPanelHit panelHits[2];
                if (xrPanelVisible_ && panelTracked &&
                    _shellIconManager.interfaceShowFlag()) {
                    for (unsigned hand = 0; hand < 2; ++hand)
                        panelHits[hand].valid = androidXrHitUiPanel(
                            xrInput.hands[hand], uiWidth, uiHeight,
                            &panelHits[hand].x, &panelHits[hand].y);
                }
                const bool zeroplastMode = m_ShellDispatcher.m_nEditRegion == editRegion1 &&
                    CurrentMission.gameType_ != GT_PLAY_RELL;
                const bool zeroplastControlsEnabled = zeroplastMode &&
                    !xrTableCameraLocked && !isPaused() &&
                    _shellIconManager.interfaceShowFlag();
                // A tool switch already submits through CancelEditWorkarea.
                if (!zeroplastMode)
                    xrZeroplastHand_ = -1;
                else if (!zeroplastControlsEnabled || xrInput.recentered)
                    finishXrBrushStroke();
                unsigned pointerHand = chooseXrPointerHand(xrInput, panelHits,
                    xrUiPressCaptured_ ? xrUiPressHand_ : -1);
                if (xrZeroplastHand_ >= 0)
                    pointerHand = static_cast<unsigned>(xrZeroplastHand_);
                else if (BuildingInstallerInited() || (zeroplastMode && !xrUiPressCaptured_))
                    pointerHand = xrInput.hands[1].aimValid ? 1u : 0u;
                Vect2f pointerPosition = mousePosition_;
                const bool pointerOverUi = panelHits[pointerHand].valid &&
                    !BuildingInstallerInited() && xrZeroplastHand_ < 0;
                uiPointerVisible = pointerOverUi;
                if (pointerOverUi) {
                    uiPointerX = panelHits[pointerHand].x;
                    uiPointerY = panelHits[pointerHand].y;
                    pointerPosition.set(uiPointerX / uiWidth - 0.5f,
                                        uiPointerY / uiHeight - 0.5f);
                    mousePositionDelta_ = pointerPosition - mousePosition_;
                    mousePosition_ = pointerPosition;
                    CursorOverInterface = _shellIconManager.OnMouseMove(
                        pointerPosition.x + 0.5f, pointerPosition.y + 0.5f);
                    if (endXrFrameIfMissionChanged()) return;
                    m_ShellDispatcher.OnMouseMove(pointerPosition.x + 0.5f,
                                                  pointerPosition.y + 0.5f);
                    if (endXrFrameIfMissionChanged()) return;
                }

                if (xrUiPressCaptured_) {
                    const bool released = xrUiPressHand_ >= 0 &&
                        (xrInput.hands[xrUiPressHand_].released & ANDROID_XR_SELECT);
                    const bool trackingLost = xrUiPressHand_ < 0 ||
                        !xrInput.hands[xrUiPressHand_].aimValid;
                    if (released) {
                        const float x = pointerPosition.x + 0.5f;
                        const float y = pointerPosition.y + 0.5f;
                        const bool uiHandled = _shellIconManager.OnLButtonUp(x, y);
                        if (endXrFrameIfMissionChanged()) {
                            xrUiPressCaptured_ = false;
                            xrUiPressHand_ = -1;
                            return;
                        }
                        if (!uiHandled)
                            m_ShellDispatcher.OnLButtonUp(x, y);
                        if (endXrFrameIfMissionChanged()) {
                            xrUiPressCaptured_ = false;
                            xrUiPressHand_ = -1;
                            return;
                        }
                        _shellIconManager.lButtonReset();
                        xrUiPressCaptured_ = false;
                        xrUiPressHand_ = -1;
                    } else if (trackingLost) {
                        _shellIconManager.lButtonReset();
                        xrUiPressCaptured_ = false;
                        xrUiPressHand_ = -1;
                    }
                }

                if (BuildingInstallerInited()) {
                    const auto& hand = xrInput.hands[pointerHand];
                    if (!hand.aimValid || (hand.pressed & ANDROID_XR_CANCEL)) {
                        BuildingInstaller->CancelObject();
                        xrBuildAngle_ = 0.0f;
                    } else if (panelHits[pointerHand].valid) {
                        BuildingInstaller->HideWorldPosition();
                    } else {
                        if (hand.pressed & ANDROID_XR_ROTATE)
                            xrBuildAngle_ += XM_PI / 4.0f;
                        const MatXf worldAim = centerWorld *
                            xrCameraRig_->Pose(hand.aimPosition, hand.aimOrientation);
                        Vect3f ground;
                        if (terScene->Trace(worldAim.trans(),
                                            worldAim * Vect3f(0, 0, 5000),
                                            &ground, false, false)) {
                            BuildingInstaller->SetBuildPositionWorld(ground, xrBuildAngle_,
                                                                     universe()->activePlayer());
                            if ((hand.pressed & ANDROID_XR_SELECT) && BuildingInstaller->valid()) {
                                BuildingInstaller->ConstructObject(universe()->activePlayer());
                                xrBuildAngle_ = 0.0f;
                            }
                        } else {
                            BuildingInstaller->HideWorldPosition();
                        }
                    }
                } else if (zeroplastControlsEnabled && !xrUiPressCaptured_ &&
                           (xrZeroplastHand_ >= 0 || !panelHits[pointerHand].valid)) {
                    const auto& hand = xrInput.hands[pointerHand];
                    if (!hand.aimValid || (hand.pressed & ANDROID_XR_CANCEL)) {
                        xrZeroplastHand_ = -1;
                        CancelEditWorkarea();
                    } else {
                        if (hand.released & ANDROID_XR_SELECT)
                            finishXrBrushStroke();
                        const MatXf worldAim = centerWorld *
                            xrCameraRig_->Pose(hand.aimPosition, hand.aimOrientation);
                        Vect3f ground;
                        if (!panelHits[pointerHand].valid && !xrInput.recentered &&
                            terScene->Trace(worldAim.trans(),
                                            worldAim * Vect3f(0, 0, 5000),
                                            &ground, false, false)) {
                            if (hand.pressed & ANDROID_XR_SELECT)
                                xrZeroplastHand_ = static_cast<int>(pointerHand);
                            xrBrushPosition =
                                m_ShellDispatcher.UpdateXrZeroplastBrush(
                                    ground, xrZeroplastHand_ >= 0, xrBrushRadius);
                        }
                    }
                } else {
                    for (unsigned handIndex = 0; handIndex < 2; ++handIndex) {
                        const auto& input = xrInput.hands[handIndex];
                        if (!input.aimValid) continue;
                        if (xrUiPressCaptured_ && xrUiPressHand_ == static_cast<int>(handIndex))
                            continue;
                        if (zeroplastMode && (input.pressed & ANDROID_XR_CANCEL)) {
                            CancelEditWorkarea();
                            continue;
                        }
                        if (panelHits[handIndex].valid) {
                            if ((input.pressed & ANDROID_XR_SELECT) && !xrUiPressCaptured_) {
                                const float x = panelHits[handIndex].x / uiWidth;
                                const float y = panelHits[handIndex].y / uiHeight;
                                const bool uiHandled = _shellIconManager.OnLButtonDown(x, y);
                                if (endXrFrameIfMissionChanged()) return;
                                if (!uiHandled)
                                    m_ShellDispatcher.OnLButtonDown(x, y);
                                if (endXrFrameIfMissionChanged()) return;
                                xrUiPressCaptured_ = true;
                                xrUiPressHand_ = static_cast<int>(handIndex);
                            }
                            continue;
                        }
                        // A chosen work-area tool owns world trigger input.
                        if (zeroplastMode || m_ShellDispatcher.m_nEditRegion == editRegion1)
                            continue;
                        if (input.pressed & ANDROID_XR_CANCEL) {
                            universe()->DeselectAll();
                            continue;
                        }
                        if ((input.pressed & (ANDROID_XR_SELECT | ANDROID_XR_COMMAND)) == 0)
                            continue;

                        const MatXf worldAim = centerWorld *
                            xrCameraRig_->Pose(input.aimPosition, input.aimOrientation);
                        const Vect3f rayStart = worldAim.trans();
                        const Vect3f rayFinish = worldAim * Vect3f(0, 0, 5000);
                        Vect3f ground;
                        const bool groundHit = terScene->Trace(rayStart, rayFinish,
                                                               &ground, false, false);
                        if (input.pressed & ANDROID_XR_SELECT)
                            universe()->select.selectUnitRay(rayStart,
                                groundHit ? ground : rayFinish, COMMAND_SELECTED_MODE_NONE);
                        if ((input.pressed & ANDROID_XR_COMMAND) && groundHit)
                            universe()->makeCommandSubtle(COMMAND_ID_POINT, ground,
                                                           COMMAND_SELECTED_MODE_NONE);
                    }
                }
            } else {
                finishXrBrushStroke(true);
                if (xrUiPressCaptured_)
                    _shellIconManager.lButtonReset();
                xrUiPressCaptured_ = false;
                xrUiPressHand_ = -1;
            }

            if (endXrFrameIfMissionChanged()) return;
            // Refresh selection after XR input and model interpolation, so its
            // circles appear in the same frame as the selected unit's bar.
            {
                MTAutoSingleThread logicLock;
                universe()->select.ShowCircles();
            }
            terScene->PrepareTerrainViewFamily(xrEyeCameras_[0], xrEyeCameras_[1]);
            if (_shellIconManager.interfaceShowFlag())
                universe()->PrepareShowInfo();
            // Keep transient placement and brush circles for both eye views.
            if (xrBrushRadius > 0.0f)
                terCircleShowGraph(xrBrushPosition, xrBrushRadius,
                                  circleColors.zeroLayerRadius);
            gbCircleShow->BeginStereoDraw();
            androidXrSetUiPanelVisible(xrInput.focused && xrPanelVisible_ &&
                panelTracked && _shellIconManager.interfaceShowFlag());
            float laserDistances[2];
            getXrControllerLaserDistances(xrInput, *xrCameraRig_, centerWorld,
                uiWidth, uiHeight, laserDistances, true, skyCenter, skyRadius);
            const bool rendered = drawXrEyeViews(terRenderDevice, xrViews, [&](unsigned eye) {
                cCamera* camera = xrEyeCameras_[eye];
                terRenderDevice->SetRenderState(RS_FOGENABLE, false);
                terScene->DrawView(camera);
                // DrawView restores the logical UI scissor, but world overlays
                // are projected into the full eye target.
                terRenderDevice->SetClipRect(0, 0,
                    static_cast<int>(xrViews[eye].width),
                    static_cast<int>(xrViews[eye].height));
                if (_shellIconManager.interfaceShowFlag())
                    universe()->ShowInfo(false);
                showWays();
                for (unsigned hand = 0; hand < 2; ++hand) {
                    drawXrControllerLaser(terRenderDevice, camera,
                        xrCameraRig_->UnitsPerMeter(), xrViews[eye],
                        xrInput.hands[hand], laserDistances[hand],
                        hand == 0 ? sColor4c(64, 180, 255, 255)
                                                       : sColor4c(255, 180, 64, 255));
                }
            });
            gbCircleShow->EndStereoDraw();
            if (rendered && xrInput.focused && xrPanelVisible_ && panelTracked &&
                _shellIconManager.interfaceShowFlag())
                drawXrUiPanel(terRenderDevice, &m_ShellDispatcher,
                              uiWidth, uiHeight, uiPointerVisible, uiPointerX, uiPointerY);
            androidXrEndFrame(rendered);
            m_ShellDispatcher.PostDraw();
            terScene->PostDraw(centerCamera);
            return;
        }
        androidXrClearListenerView();
        if (!androidXrSessionActive() || !androidXrIsFocused())
            finishXrBrushStroke(true);
        if (androidXrSessionActive() && BuildingInstallerInited() &&
            !androidXrIsFocused()) {
            BuildingInstaller->CancelObject();
            xrBuildAngle_ = 0.0f;
        }
        if (androidXrSessionActive() && !androidXrIsFocused()) {
            if (xrUiPressCaptured_)
                _shellIconManager.lButtonReset();
            xrUiPressCaptured_ = false;
            xrUiPressHand_ = -1;
        }
#endif

		terRenderDevice->Fill(0,0,0);
		terRenderDevice->BeginScene();

		if(FogStart>=0 && FogEnd>0)//terFogStart=300,terFogEnd=1000
				terRenderDevice->SetGlobalFog(FogColor,Vect2f(FogStart,FogEnd));
		
		if(chaos)
			chaos->Draw();

		if(showWireFrame_)
			terRenderDevice->SetRenderState(RS_WIREFRAME,1);

		terScene->Draw(terCamera->GetCamera());

		if(showWireFrame_)
			terRenderDevice->SetRenderState(RS_WIREFRAME,0);

		if(_shellIconManager.interfaceShowFlag())
			universe()->ShowInfo();

		showWays();		

		terRenderDevice->SetDrawTransform(terCamera->GetCamera());
		if (debug_show_mode) {
            MTAutoSingleThread debug_show_lock;
			universe()->showDebugInfo();
			show_dispatcher.draw();
		}

		if(missionEditor_)
			missionEditor_->quant();

		terRenderDevice->FlushPrimitive3D();
		terRenderDevice->SetClipRect(0,0,terRenderDevice->GetSizeX(),terRenderDevice->GetSizeY());

		if (bgScene->ready()) {
			bgScene->quant(frame_time.delta());
			bgScene->preDraw();
			bgScene->draw();
			bgScene->postDraw();
		}

		_shellIconManager.draw();
		m_ShellDispatcher.draw();

		if (autoSwitchAIEnabled) {
			checkAutoswitchAI();
		}

		gb_VisGeneric->DrawInfo();
		ai_tile_map->drawWalkMap();
//		terCamera->GetCamera()->DrawTestGrid();
		HTManager::instance()->Show();

        renderEndScene();

		if(recordMovie_)
			makeMovieShot();

		terRenderDevice->Flush();

		m_ShellDispatcher.PostDraw();

		terScene->PostDraw(terCamera->GetCamera());

		if(debug_write_mode & DEBUG_WRITE_BODY_STATE)
			universe()->WriteDebugInfo();
		if(debug_write_mode & DEBUG_SHOW_WATCH)
			show_watch();
	}
	else{
		//quant
		if(bwScene->ready()){
			bwScene->quant(mousePosition(), frame_time.delta());
		} else if (!historyScene->ready()) {
		} else {
			historyScene->quant(mousePosition(), frame_time.delta());
		}

		if (bgScene->ready()) {
			bgScene->quant(frame_time.delta());
		}

#if defined(ANDROID_XR)
        AndroidXrEyeView menuViews[2]{};
        AndroidXrInputFrame menuInput{};
        if (androidXrBeginFrame(menuViews, &menuInput)) {
            if (menuInput.recentered) terCamera->recenterOrientation();
            cCamera* centerCamera = terCamera->GetCamera();
            if (!xrCameraRig_) xrCameraRig_ = new XrCameraRig();
            // Keep the authored menu heading frame through the history
            // briefing so its 3D scene stays aligned with the fixed UI panel.
            const bool menuBackdrop = bwScene->ready();
            const bool menuHeadingAligned = menuBackdrop || historyScene->ready();
            const bool menuSceneReady = bgScene->ready();
            const bool menuMotionControlsEnabled = !menuSceneReady;
            // Anchor once after the menu backdrop first loads. Keep the panel
            // in that world position through screen transitions, even if the
            // backdrop briefly becomes unready while entering the briefing.
            if (menuSceneReady && (!xrMenuPanelPoseAnchored_ || menuInput.recentered))
                androidXrResetUiPanelPose();
            if (menuSceneReady) xrMenuPanelPoseAnchored_ = true;
            xrCameraRig_->SetMenuHeadingAligned(menuHeadingAligned);
            xrCameraRig_->BeginFrame(menuViews, menuInput.recentered);
            float headPosition[3]{};
            getXrHeadPosition(menuViews, headPosition);
            const float deltaSeconds = frame_time.delta() * 0.001f;
            MatXf centerWorld = centerCamera->GetMatrix();
            centerWorld.invert();
            const Vect3f tablePivot =
                menuMotionControlsEnabled && menuInput.focused &&
                    xrCameraRig_->NeedsTablePivot(
                        menuInput.hands[1].thumbstick[1],
                        menuInput.hands[1].thumbstickActive)
                    ? getXrScalePivot(*xrCameraRig_, centerWorld, menuViews)
                    : Vect3f::ZERO;
            xrCameraRig_->UpdateControls(
                menuInput.hands[0].thumbstick,
                menuMotionControlsEnabled && menuInput.focused &&
                    menuInput.hands[0].thumbstickActive,
                menuMotionControlsEnabled ? menuInput.hands[1].thumbstick[1] : 0.0f,
                menuMotionControlsEnabled && menuInput.focused &&
                    menuInput.hands[1].thumbstickActive,
                deltaSeconds, tablePivot, centerWorld, headPosition,
                Vect3f::ZERO, 0.0f);
            publishXrListenerView(*xrCameraRig_, centerWorld, menuViews,
                                  headPosition, menuInput.focused);
            prepareXrEyeCameras(*xrCameraRig_, terScene, centerCamera,
                                xrEyeCameras_, menuViews);
            const unsigned uiWidth = static_cast<unsigned>(terRenderDevice->GetSizeX());
            const unsigned uiHeight = static_cast<unsigned>(terRenderDevice->GetSizeY());
            if (bgScene->ready()) {
                const float unitsPerMeter = xrCameraRig_->UnitsPerMeter();
                constexpr float menuPanelForwardOffsetMeters = 0.5f;
                const float menuPanelDistanceMeters =
                    bgScene->xrMenuPanelDistanceUnits() / unitsPerMeter;
                float panelDistanceMeters =
                    menuPanelDistanceMeters - menuPanelForwardOffsetMeters;
                if (panelDistanceMeters < 0.05f) panelDistanceMeters = 0.05f;
                const float panelSizeScale =
                    panelDistanceMeters / menuPanelDistanceMeters;
                androidXrSetUiPanelFixed(panelDistanceMeters,
                    bgScene->xrMenuPanelWidthUnits() / unitsPerMeter * panelSizeScale);
            } else {
                androidXrSetUiPanelFixed();
            }
            androidXrPrepareUiPanel(uiWidth, uiHeight);

            bool uiPointerVisible = false;
            float uiPointerX = 0.0f;
            float uiPointerY = 0.0f;
            if (menuInput.focused) {
                if (xrUiPressCaptured_ && !_shellIconManager.interfaceShowFlag()) {
                    _shellIconManager.lButtonReset();
                    xrUiPressCaptured_ = false;
                    xrUiPressHand_ = -1;
                }
                androidXrSetUiPanelVisible(_shellIconManager.interfaceShowFlag());
                XrPanelHit panelHits[2];
                if (_shellIconManager.interfaceShowFlag()) {
                    for (unsigned hand = 0; hand < 2; ++hand)
                        panelHits[hand].valid = androidXrHitUiPanel(
                            menuInput.hands[hand], uiWidth, uiHeight,
                            &panelHits[hand].x, &panelHits[hand].y);
                }
                const unsigned pointerHand = chooseXrPointerHand(
                    menuInput, panelHits, xrUiPressCaptured_ ? xrUiPressHand_ : -1);
                const bool pointerOverPanel = panelHits[pointerHand].valid;
                uiPointerVisible = pointerOverPanel;
                if (pointerOverPanel) {
                    uiPointerX = panelHits[pointerHand].x;
                    uiPointerY = panelHits[pointerHand].y;
                    mousePosition_.set(uiPointerX / uiWidth - 0.5f,
                                       uiPointerY / uiHeight - 0.5f);
                    _shellIconManager.OnMouseMove(mousePosition_.x + 0.5f,
                                                  mousePosition_.y + 0.5f);
                    m_ShellDispatcher.OnMouseMove(mousePosition_.x + 0.5f,
                                                  mousePosition_.y + 0.5f);
                }

                if (xrUiPressCaptured_) {
                    const bool released = xrUiPressHand_ >= 0 &&
                        (menuInput.hands[xrUiPressHand_].released & ANDROID_XR_SELECT);
                    const bool trackingLost = xrUiPressHand_ < 0 ||
                        !menuInput.hands[xrUiPressHand_].aimValid;
                    if (released) {
                        const float x = mousePosition_.x + 0.5f;
                        const float y = mousePosition_.y + 0.5f;
                        if (!_shellIconManager.OnLButtonUp(x, y))
                            m_ShellDispatcher.OnLButtonUp(x, y);
                        _shellIconManager.lButtonReset();
                        xrUiPressCaptured_ = false;
                        xrUiPressHand_ = -1;
                    } else if (trackingLost) {
                        _shellIconManager.lButtonReset();
                        xrUiPressCaptured_ = false;
                        xrUiPressHand_ = -1;
                    }
                }
                if (!xrUiPressCaptured_ && pointerOverPanel &&
                    (menuInput.hands[pointerHand].pressed & ANDROID_XR_SELECT)) {
                    const float x = panelHits[pointerHand].x / uiWidth;
                    const float y = panelHits[pointerHand].y / uiHeight;
                    if (!_shellIconManager.OnLButtonDown(x, y))
                        m_ShellDispatcher.OnLButtonDown(x, y);
                    xrUiPressCaptured_ = true;
                    xrUiPressHand_ = static_cast<int>(pointerHand);
                }
            } else {
                if (xrUiPressCaptured_)
                    _shellIconManager.lButtonReset();
                xrUiPressCaptured_ = false;
                xrUiPressHand_ = -1;
            }

            // A menu selection can enter the mission and dispose menu scenes
            // while this OpenXR frame is still open.
            if (GameActive) {
                androidXrEndFrame(false);
                return;
            }

            const bool menuUiVisible = menuInput.focused &&
                _shellIconManager.interfaceShowFlag();
            androidXrSetUiPanelVisible(menuUiVisible);
            // Draw the UI once before the eye passes so each eye can blend it
            // directly over its own menu scene at the panel's fixed pose.
            bool menuPanelReady = !menuUiVisible ||
                drawXrUiPanel(terRenderDevice, nullptr,
                              uiWidth, uiHeight, uiPointerVisible, uiPointerX, uiPointerY);
            // The menu has its own scenes and cameras. Prepare each scene once,
            // then render its animated objects from both headset eye poses.
            HistoryScene* const menuHistoryScene = bwScene->ready() ? bwScene :
                (historyScene->ready() ? historyScene : nullptr);
            const bool menuBackdropReady = bgScene->ready();
            if (menuHistoryScene) {
                menuHistoryScene->prepareXrViews(*xrCameraRig_, menuViews);
            } else {
                terScene->dSetTime(frame_time.delta());
                terScene->PreDraw(centerCamera);
                terScene->PrepareViewFamily();
            }
            if (menuBackdropReady)
                bgScene->prepareXrViews(*xrCameraRig_, menuViews);
            float laserDistances[2];
            getXrControllerLaserDistances(menuInput, *xrCameraRig_, centerWorld,
                uiWidth, uiHeight, laserDistances, false);
            const bool rendered = drawXrEyeViews(terRenderDevice, menuViews, [&](unsigned eye) {
                cCamera* menuCamera = xrEyeCameras_[eye];
                if (menuHistoryScene) {
                    menuHistoryScene->drawXrView(eye);
                    menuCamera = menuHistoryScene->xrCamera(eye);
                } else {
                    terScene->DrawView(xrEyeCameras_[eye]);
                }
                if (menuBackdropReady) {
                    if (!menuHistoryScene) {
                        // Shade the base scene before drawing the menu meshes.
                        terRenderDevice->DrawRectangle(0, 0,
                            static_cast<int>(menuViews[eye].width),
                            static_cast<int>(menuViews[eye].height),
                            sColor4c(0, 0, 0, 96));
                        terRenderDevice->FlushPrimitive2D();
                    }
                    bgScene->drawXrView(eye);
                    menuCamera = bgScene->xrCamera(eye);
                }
                terRenderDevice->SetClipRect(0, 0,
                    static_cast<int>(menuViews[eye].width),
                    static_cast<int>(menuViews[eye].height));
                for (unsigned hand = 0; hand < 2; ++hand) {
                    drawXrControllerLaser(terRenderDevice, menuCamera,
                        xrCameraRig_->UnitsPerMeter(), menuViews[eye],
                        menuInput.hands[hand], laserDistances[hand],
                        hand == 0 ? sColor4c(64, 180, 255, 255)
                                                         : sColor4c(255, 180, 64, 255));
                }
                terRenderDevice->FlushPrimitive3D();
                if (menuUiVisible && menuPanelReady)
                    menuPanelReady = androidXrDrawUiPanelInEye(terRenderDevice, eye);
            });
            androidXrEndFrame(rendered && menuPanelReady);
            if (menuHistoryScene) menuHistoryScene->postDraw();
            else terScene->PostDraw(centerCamera);
            if (menuBackdropReady) bgScene->postDraw();
            return;
        }
        androidXrClearListenerView();
        if (androidXrSessionActive() && !androidXrIsFocused()) {
            if (xrUiPressCaptured_)
                _shellIconManager.lButtonReset();
            xrUiPressCaptured_ = false;
            xrUiPressHand_ = -1;
        }
#endif
		
		//draw
		terRenderDevice->Fill(0,0,0);
		terRenderDevice->BeginScene();

		if (bwScene->ready()) {
//			bwScene->quant(mousePosition(), frame_time.delta());
			if (bwScene->ready()) {
				bwScene->preDraw();
				bwScene->draw();
				bwScene->postDraw();
			}
		} else if (!historyScene->ready()) {
			terScene->dSetTime(frame_time.delta());
			terScene->PreDraw(terCamera->GetCamera());
			terScene->Draw(terCamera->GetCamera());
		} else {
//			historyScene->quant(mousePosition(), frame_time.delta());
			if (historyScene->ready()) {
				historyScene->preDraw();
				historyScene->draw();
				historyScene->postDraw();
			}
		}

		if (bgScene->ready()) {
//			bgScene->quant(frame_time.delta());
			bgScene->preDraw();
			bgScene->draw();
			bgScene->postDraw();
		}
		_shellIconManager.draw();

		//m_ShellDispatcher.draw();

        renderEndScene();

		terRenderDevice->Flush();
		terScene->PostDraw(terCamera->GetCamera());
	}


/*	if(terEnableGDIPixel)
	{
		terRenderDevice->OutText(0,0,"A",255,255,255,"Arial");
//		HDC hDC=GetDC(terRenderDevice->GetWindowHandle());
//		SetPixel(hDC,0,0,RGB(255,255,255));
	}
*/
}

//--------------------------------------------------------
Vect2f GameShell::convert(int x, int y) const 
{
	return Vect2f(float(x)/float(windowClientSize().x) - 0.5f, float(y)/float(windowClientSize().y) - 0.5f);
}

Vect2i GameShell::convertToScreenAbsolute(const Vect2f& pos)
{
    return Vect2i((pos.x + 0.5f)*(float)windowClientSize().x, (pos.y + 0.5f)*(float)windowClientSize().y);
}

void GameShell::EventHandler(SDL_Event& event) {
#ifdef __ANDROID__
    // Android touch gestures request camera pan directly instead of emulating a bound key.
    if (event.type == androidTouchCameraDragEventType()) {
        if (event.user.code != 0) {
            if (!_bMenuMode && !reelManager.isVisible() && !isScriptReelEnabled() &&
                !_shellIconManager.isCutSceneMode() && !cameraMouseTrack && !cameraMouseShift) {
                setCameraMouseShift(true);
            }
        } else {
            setCameraMouseShift(false);
        }
        return;
    }
#endif

    if (reelManager.isVisible()) {
        if (reelAbortEnabled) {
            switch (event.type) {
                case SDL_KEYUP: {
                    int key = sKey(event.key.keysym).fullkey;
                    if (key == VK_SPACE || key == VK_ESCAPE || key == VK_END) {
                        reelManager.hide();
                    }
                    break;
                }
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button & (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)) {
                        reelManager.hide();
                    }
                    break;
                default:
                    break;
            }
        }
        if (!GameContinue) {
            reelManager.hide();
        }
        return;
    }

#ifdef __ANDROID__
    if (event.type == androidTouchTwoFingerGestureEventType()) {
        Uint32 packed = 0;
        std::memcpy(&packed, &event.user.code, sizeof(packed));
        const float verticalWheelDelta =
            (static_cast<int>((packed >> 16) & 0xffff) - 32768) / 1024.0f;
        const float pinchZoomDelta =
            (static_cast<int>(packed & 0xffff) - 32768) / 1024.0f;

        if (_bMenuMode) {
            MouseWheel(-verticalWheelDelta * 2.0f, true);
        } else if (pinchZoomDelta != 0.0f) {
            MouseWheel(pinchZoomDelta);
        }
        return;
    }

    if (event.type == androidTouchCameraRotationEventType()) {
        Uint32 packed = 0;
        std::memcpy(&packed, &event.user.code, sizeof(packed));
        const float horizontalDelta =
            (static_cast<int>((packed >> 16) & 0xffff) - 32768) / 1024.0f;
        const float verticalDelta =
            (static_cast<int>(packed & 0xffff) - 32768) / 1024.0f;
        applyAndroidCameraRotation(Vect2f(horizontalDelta, verticalDelta));
        return;
    }
#endif

    //Sets the SDL2 text input mode according to current text edit mode in UI
    bool text_input_active = SDL_TRUE == SDL_IsTextInputActive();
    if (_shellIconManager.isInEditMode() != text_input_active) {
        if (_shellIconManager.isInEditMode()) {
            SDL_StartTextInput();
        } else {
            SDL_StopTextInput();
        }
    }

    sKey s;
    switch (event.type) {
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            bool pressed = event.button.state == SDL_PRESSED;
            bool doubleClick = false;
            if (!pressed) {
                float dist = xm::abs(event.button.x - lastClickPosition.x) + xm::abs(event.button.y - lastClickPosition.y);
                doubleClick = (clockf() - lastClickTime) < doubleClickTime && dist < doubleClickDistance && lastClickButton == event.button.button;
                lastClickPosition.set(event.button.x, event.button.y);
                lastClickTime = clockf();
                if (doubleClick) lastClickTime -= doubleClickTime;
                lastClickButton = event.button.button;
            }
            Vect2f where = convert(event.button.x, event.button.y);
            //printf("M %fx%f B %dn", where.x, where.y, event.button.button, pressed);
            if (terRenderDevice->DebugUIIsEnabled()
            && terRenderDevice->DebugUIMousePress(where, event.button.button, pressed)) {
                break;
            }
            switch (event.button.button) {
                case SDL_BUTTON_LEFT:
                    if (doubleClick) {
                        MouseLeftPressed(where);
                        MouseLeftUnpressed(where);
                        MouseLeftDoubleClick(where);
                    } else {
                        if (pressed) {
                            MouseLeftPressed(where);
                        } else {
                            MouseLeftUnpressed(where);
                        }
                    }
                    break;
                case SDL_BUTTON_RIGHT:
                    if (doubleClick) {
                        MouseRightPressed(where);
                        MouseRightUnpressed(where);
                        MouseRightDoubleClick(where);
                    } else {
                        if (pressed) {
                            MouseRightPressed(where);
                        } else {
                            MouseRightUnpressed(where);
                        }
                    }
                    break;
                case SDL_BUTTON_MIDDLE:
                    MouseButton(where, VK_MBUTTON, pressed);
                    break;
                case SDL_BUTTON_X1:
                    MouseButton(where, VK_XBUTTON1, pressed);
                    break;
                case SDL_BUTTON_X2:
                    MouseButton(where, VK_XBUTTON2, pressed);
                    break;
                default:
                    break;
            }
            break;
        }
        case SDL_MOUSEWHEEL: {
            bool normal = event.wheel.direction == SDL_MOUSEWHEEL_NORMAL;
            float delta = event.wheel.preciseY * (normal ? 1.0f : -1.0f);
            if (delta != 0) {
                MouseWheel(delta);
            }
            break;
        }
        case SDL_MOUSEMOTION: {
            Vect2f where = convert(event.motion.x, event.motion.y);
            //printf("M %fx%f\n", where.x, where.y * 100);
            MouseMove(where, Vect2f(
                static_cast<float>(event.motion.xrel),
                static_cast<float>(event.motion.yrel)
            ));
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool editMode = _shellIconManager.isInEditMode();
            SDL_KeyboardEvent key = event.key;
            s = sKey(key.keysym);
            if (editMode && s.fullkey == ('V' | KBD_CTRL)) {
                //Pasting keycombo, discard normal keydown/up
                if (key.state == SDL_PRESSED && SDL_HasClipboardText()) {
                    char* text = SDL_GetClipboardText();
                    if (text && *text != '\0') {
                        std::string codepaged = convertToCodepage(text, getLocale());
                        for (auto& c : codepaged) {
                            if (0 <= c && c < ' ') break;
                            _shellIconManager.OnChar(c);
                        }
                    }
                    SDL_free(text);
                }
            } else {
                if (key.state == SDL_PRESSED) {
                    KeyPressed(s);

                    //We need to send certain keys when editing as SDL_TEXTINPUT don't receive them
                    if (editMode) {
                        switch (s.key) {
                            case VK_BACK:
                            case VK_RETURN:
                                _shellIconManager.OnChar(static_cast<char>(s.key));
                                break;
                            default:
                                break;
                        }
                    }
                } else {
                    KeyUnpressed(s);
                }
            }
            break;
        }
        case SDL_TEXTINPUT: {
            //NOTE: _shellIconManager.isInEditMode() is implicit here as SDL2 edit mode is changed accordingly
            //printf("TI %s\n", event.text.text);
            std::string key = convertToCodepage(event.text.text, getLocale());
            if (!key.empty()) {
                _shellIconManager.OnChar(key.front());
            }
            break;
        }
        case SDL_TEXTEDITING: {
            //printf("TE %s S %d L %d\n", event.edit.text,  event.edit.start, event.edit.length);
            break;
        }
        case SDL_WINDOWEVENT: {
            switch (event.window.event) {
                case SDL_WINDOWEVENT_FOCUS_GAINED: {
                    OnWindowActivate();
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default: {
            break;
        }
    }
}

void GameShell::DebugCteateFilth(terFilthSpotID id)
{
	Vect3f v;
	if(universe()->worldPlayer() && terCamera->cursorTrace(mousePosition(),v))
	{
		terFilthSpot* p = safe_cast<terFilthSpot*>(universe()->worldPlayer()->buildUnit(UNIT_ATTRIBUTE_FILTH_SPOT));
		p->setPose(Se3f(QuatF::ID, v), true);
		p->SetFilthParamID(id);
		p->Start();
	}
}

bool GameShell::DebugKeyPressed(sKey& Key)
{
	MTAutoSingleThread mtAutoSkipAssert;

	switch(Key.fullkey){
	case VK_F1:
		showKeysHelp_ = !showKeysHelp_;
		return false;
	case VK_F9:
		terCamera->addCurrentToPath();
		break;
	case VK_F9 | KBD_SHIFT:
		terCamera->removeLastPointFromPath();
		break;
	case VK_F9 | KBD_CTRL | KBD_SHIFT:
		terCamera->erasePath();
		break;
    case 'C' | KBD_CTRL | KBD_SHIFT: {
        MissionDescription md;
        gameShell->universalSave(nullptr, true, &md);
        universe()->universalLoad(md, this->savePrm(), nullptr);
        break;
    }
    case 'V' | KBD_CTRL | KBD_SHIFT: {
        MissionDescription md;
        gameShell->universalSave(nullptr, true, &md);
        HTManager::instance()->setMissionToStart(md);
        break;
    }
	case VK_F9 | KBD_CTRL: {
		if(!terCamera->isPlayingBack()){
			const char* name = manualData().popupCameraSplineName();
			if(name){
				const SaveCameraSplineData* spline = manualData().findCameraSpline(name);
				if(spline)
					terCamera->loadPath(*spline, false);
			}
			terCamera->startReplayPath(CAMERA_REPLAY_DURATION, 10);
		} else {
			terCamera->stopReplayPath();
        }
		break;
		}
	case VK_F10 | KBD_CTRL: {
		savePrm().manualData.cameras.push_back(SaveCameraSplineData());
		terCamera->savePath(savePrm().manualData.cameras.back());
		XBuffer name;
		name < "Camera" <= static_cast<uint32_t>(manualData().cameras.size());
		savePrm().manualData.cameras.back().name = editText(name);
		SavePrm data;
		CurrentMission.loadMission(data);
		data.manualData = manualData();
		CurrentMission.saveMission(data, false);
		} break; 
		
	case VK_F11 | KBD_CTRL:
		startStopRecordMovie();
		break;
	case VK_F12 | KBD_CTRL | KBD_SHIFT: 
		m_ShellDispatcher.OnInterfaceMessage(UNIVERSE_INTERFACE_MESSAGE_GAME_VICTORY);
		break; 
	case VK_F12 | KBD_SHIFT: 
		m_ShellDispatcher.OnInterfaceMessage(UNIVERSE_INTERFACE_MESSAGE_GAME_DEFEAT);
		break; 
	case VK_F5:
		{
			void UpdateRegionMap(int x1,int y1,int x2,int y2);
			vMap.toShowDbgInfo(!(vMap.IsShowDbgInfo()));
			UpdateRegionMap(0, 0, vMap.H_SIZE-1, vMap.V_SIZE-1);
		}
		break;
	case VK_F2:
			if(isShiftPressed())
				debug_write_mode ^= DEBUG_WRITE_BODY_STATE;
			else
				debug_write_mode ^= DEBUG_SHOW_WATCH;
		if(!debug_write_mode){
//			hide_debug_window();
            SDL_ShowCursor(SDL_FALSE);
			}
		break;
	case VK_F3:
		debug_show_mode ^= 1;
		break;

	case VK_F4 | KBD_SHIFT:
	case VK_F4:
		if(isAltPressed())
			break;
		editParameters();
		break;

#ifdef PERIMETER_DEBUG
	case VK_RETURN | KBD_CTRL: 
	case VK_RETURN | KBD_CTRL | KBD_SHIFT: {
		terRenderDevice->Flush(true);
        SDL_ShowCursor(SDL_TRUE);
		//setUseAlternativeNames(true);
#ifndef _FINAL_VERSION_
        //TODO Port TriggerChain to ingame dev UI instead of using win32 stuff
		static TriggerEditor triggerEditor(triggerInterface());
		TriggerChain* triggerChain = universe()->activePlayer()->getStrategyToEdit();
		if(triggerChain && triggerEditor.run(*triggerChain, hWndVisGeneric)){
			triggerChain->initializeTriggersAndLinks();
			triggerChain->save();
			triggerChain->buildLinks();

			SavePrm data;
			CurrentMission.loadMission(data);
			data.manualData = manualData();
			CurrentMission.saveMission(data, false);
		}
#endif

		terCamera->setFocus(HardwareCameraFocus);
        SDL_ShowCursor(SDL_FALSE);
		RestoreFocus();									
		break;
	}
#endif

	case VK_F4 | KBD_CTRL:
		if(!missionEditor_){
			setCutSceneMode(false, false);
			missionEditor_ = new MissionEditor;
		} else {
			delete missionEditor_;
			missionEditor_ = nullptr;
		}
		break;

	case VK_F6:
#ifdef PERIMETER_DEBUG
		if (isShiftPressed()) {
			terRenderDevice->StartCaptureFrame();
			break;
		}
#endif
        SDL_ShowCursor(SDL_TRUE);
		profiler_start_stop();
        SDL_ShowCursor(SDL_FALSE);
		RestoreFocus();
		break;

	case 'N': 
		debug_variation = 1 - debug_variation;
		break;

    case 'R' | KBD_CTRL:
        terRenderDevice->DebugUISetEnable(!terRenderDevice->DebugUIIsEnabled());
        break;

	case 'M':
		terCamera->setRestriction(!terCamera->restricted());
		break;
	case 219:
		extern float terMapLevelLOD, terNearDistanceLOD;
		if(terMapLevelLOD>0) terMapLevelLOD-=5;
		if(terNearDistanceLOD>5) terNearDistanceLOD-=5;
		terVisGeneric->SetMapLevel(terMapLevelLOD);
		terVisGeneric->SetNearDistanceLOD(terNearDistanceLOD);
		break;
	case 221:
		extern float terMapLevelLOD, terNearDistanceLOD;
		if(terMapLevelLOD<100-5) 
			terMapLevelLOD += 5;
		if(terNearDistanceLOD<100-5) 
			terNearDistanceLOD += 5;
		terVisGeneric->SetMapLevel(terMapLevelLOD);
		terVisGeneric->SetNearDistanceLOD(terNearDistanceLOD );
		break;

	case 'S' | KBD_CTRL | KBD_SHIFT:
		if(!missionEditor()){
			std::string name = CurrentMission.savePathContent();
			size_t pos = name.rfind(PATH_SEP);
			if(pos != std::string::npos)
				name.erase(0, pos + 1);
			name = UserSingleProfile::getAllSavesDirectory() + name;
			universalSave(name.c_str(), true);
		}
		break;

	case 'S' | KBD_CTRL: {
		std::string saveName = CurrentMission.savePathContent();
		std::string savesDir = UserSingleProfile::getAllSavesDirectory();
		if(saveFileDialog(saveName, missionEditor() ? MISSIONS_PATH : savesDir.c_str(), "spg", "Mission Name")){
			size_t pos = saveName.rfind(convert_path_content("RESOURCE") + PATH_SEP);
			if(pos != std::string::npos)
				saveName.erase(0, pos);
			universalSave(saveName.c_str(), false);
		}
		break;
		}
    case 'S' | KBD_CTRL | KBD_ALT: {
        std::string saveName = CurrentMission.savePathContent();
        universalSave(saveName.c_str(), false);
    } break;

	case 'O' | KBD_CTRL: {
		std::string saveName = CurrentMission.savePathKey();
        std::string savesDir = UserSingleProfile::getAllSavesDirectory();
		if(openFileDialog(saveName, missionEditor() ? MISSIONS_PATH : savesDir.c_str(), "spg", "Mission Name")){
			size_t pos = saveName.rfind(convert_path_content("RESOURCE") + PATH_SEP);
			if(pos != std::string::npos)
				saveName.erase(0, pos);
			//Несколько кривой участок кода, не будет работать с HT
			HTManager::instance()->GameClose();
			HTManager::instance()->GameStart(MissionDescription(saveName.c_str()));
		}
		break;
		}

	case 'O' | KBD_CTRL | KBD_SHIFT: 
		HTManager::instance()->GameClose();
		HTManager::instance()->GameStart(CurrentMission);
		break;

		
	case VK_PAUSE|KBD_SHIFT:
		alwaysRun_ = !alwaysRun_;
		break;

	case 'X':
		universe()->SetActivePlayer(++activePlayerID_ %= universe()->Players.size() - 1);
		break;
	case 'X'|KBD_SHIFT:
		universe()->activePlayer()->setAI(!universe()->activePlayer()->isAI());
        break;
	case 'G':
		terEnableGDIPixel=!terEnableGDIPixel;
		gb_VisGeneric->SetShadowMapSelf4x4(terEnableGDIPixel);
//		toggleScriptReelEnabled();
		break;

	case 'K'|KBD_SHIFT:
		universe()->makeCommand(COMMAND_ID_PRODUCTION_PAUSE_OFF,0);
		break;
	case 'K':
		universe()->makeCommand(COMMAND_ID_PRODUCTION_PAUSE_ON,0);
		break;
	case 'L':
		universe()->switchFieldTransparency();
		break;
	case VK_SPACE:
		showWireFrame_ = !showWireFrame_;
		break;
    case VK_F8 | KBD_SHIFT:
        if (!_shellIconManager.isCutSceneMode()) {
            _shellIconManager.Toggle(GameActive);
            _shellIconManager.toggleInterfaceShowFlag();
        }
        break;
    case VK_F8:
        if (!_shellIconManager.isCutSceneMode()) {
            _shellIconManager.toggleInterfaceShowFlag();
        }
        break;
	case VK_F7:
		_shellIconManager.toggleInterfaceShowFlag();
		_shellIconManager.setCutSceneMode(!_shellIconManager.interfaceShowFlag());
		break;
	case 'D':
		universe()->select.explodeUnit();
		break;
	case 'D' | KBD_CTRL | KBD_SHIFT:
		universe()->DeleteSelectedObjects();
		break;

	// Debug objects
	case 'Q':
		if(!terRenderDevice->IsFullScreen())
		{
			terFilthSpotID id=MissionEditor::SelectFilth();

			if(id!=FILTH_SPOT_ID_NONE)
			{
				DebugCteateFilth(id);
			}
		}
		break;
	case 'W':
		{
			Vect3f v;
			if(universe()->worldPlayer() && terCamera->cursorTrace(mousePosition(),v))
			{
				terUnitAttributeID id=MissionEditor::SelectGeo();
				if(id!=UNIT_ATTRIBUTE_NONE)
				{
					terGeoControl* p = safe_cast<terGeoControl*>(universe()->worldPlayer()->buildUnit(id));
					p->setPose(Se3f(QuatF::ID, v), true);
					p->Start();
				}
			}
		}
		break;
	case '0'|KBD_CTRL|KBD_SHIFT:
		gb_VisGeneric->XorShowType(SHOW_INFO);
		break;
	case '1'|KBD_CTRL|KBD_SHIFT:
		gb_VisGeneric->XorShowType(SHOW_NORMAL);
		break;
	case '2'|KBD_CTRL|KBD_SHIFT:
		gb_VisGeneric->XorShowType(SHOW_SHADOW);
		break;
	case '3'|KBD_CTRL|KBD_SHIFT:
		gb_VisGeneric->XorShowType(SHOW_REDLECTION);
		break;
	case '4'|KBD_CTRL|KBD_SHIFT:
		gb_VisGeneric->XorShowType(SHOW_TILEMAP);
		break;
	case '5'|KBD_CTRL|KBD_SHIFT:
		gb_VisGeneric->XorShowType(SHOW_OBJECT);
		break;
	case 'Z':
		decrSpeed();
		break;
	case 'A':
		incrSpeed();
		break;
	case KBD_SHIFT | 'A':
		setSpeed(1);
		break;
	case 'P'|KBD_CTRL:
		debug_show_intf_borders ^= 1;
//		abnormalNetCenterTermination();
		break;
	default:
		return false;
	}
	return true;
}

void GameShell::KeyPressed(sKey& Key)
{
    if (Key.fullkey == (VK_ESCAPE|KBD_SHIFT|KBD_CTRL)) {
        //Restart game now
        request_application_restart();
        terminate();
        return;
    }

    if (terRenderDevice->DebugUIIsEnabled()
    && terRenderDevice->DebugUIKeyPress(&Key, true)) {
        return;
    }

	if (Key.fullkey == (VK_F1|KBD_SHIFT|KBD_CTRL)) {
#ifndef PERIMETER_DEBUG
        if (check_command_line("debug_key_handler"))
#endif
        {
		    EnableDebugKeyHandlersInitial = EnableDebugKeyHandlers ^= 1;
        }
		return;
	}

    if (CaptureControlInput && CaptureControlInput(Key.fullkey, true)) {
        return;
    }

	if(_bMenuMode){
		if(EnableDebugKeyHandlers){
			if(Key.fullkey == VK_F4 && reload_parameters()){
				_shellIconManager.LoadControlsGroup(SHELL_LOAD_GROUP_MENU, true);
				_shellIconManager.SwitchMenuScreens(-1000, reloadID);
			} else if (Key.fullkey == ('P'|KBD_CTRL)) {
				debug_show_intf_borders ^= 1;
//				abnormalNetCenterTermination();
			}
		}
//		if (reelManager.isVisible()) {
//			reelManager.hide();
//		}
 		_shellIconManager.OnKeyDown(Key.fullkey);
		return;
	}

    if (missionEditor_ && missionEditor_->keyPressed(Key)) {
        return;
    }

	if (isScriptReelEnabled()) {
 		_shellIconManager.OnKeyDown(Key.fullkey);
		return;
	}

	if (EnableDebugKeyHandlers) {
		if (DebugKeyPressed(Key)) {
			return;
		}
	}

	if (_shellIconManager.isCutSceneMode() && (Key.fullkey == VK_SPACE || Key.fullkey == VK_RETURN || Key.fullkey == VK_ESCAPE)) {
		setSkipCutScene(true);
		return;
	}

	if (_shellIconManager.IsInterface() && gameShell->currentSingleProfile.getLastGameType() == UserSingleProfile::MULTIPLAYER) {
		CChatInGameEditWindow* chatEdit = (CChatInGameEditWindow*) _shellIconManager.GetWnd(SQSH_INGAME_CHAT_EDIT_ID);
		CChatInfoWindow* chatInfo = (CChatInfoWindow*) _shellIconManager.GetWnd(SQSH_CHAT_INFO_ID);
		if (Key.fullkey == VK_INSERT) {
			bool wantedAlliesMode = false;
			terPlayer* activePlayer = universe() ? universe()->activePlayer() : nullptr;
			if (activePlayer && activePlayer->frame()) {
				for (const auto& player : universe()->Players) {
					if (!player->frame()
                    || player->clan() != activePlayer->clan()
                    || player->playerID() == activePlayer->playerID()) {
                        continue;
                    }
                    RealPlayerType playerType = CurrentMission.getPlayerData(player->playerID())->realPlayerType;
                    if (playerType == REAL_PLAYER_TYPE_PLAYER
                    || playerType == REAL_PLAYER_TYPE_PLAYER_AI) {
						wantedAlliesMode = true;
						break;
					}
				}
			}
			
			if (chatEdit->isVisible()) {
				if (chatEdit->alliesOnlyMode != wantedAlliesMode) {
					chatEdit->alliesOnlyMode = wantedAlliesMode;
				} else {
					chatEdit->Show(0);
					_shellIconManager.SetFocus(0);
					chatInfo->setTime(CHATINFO_VISIBLE_TIME_AFTER_HIDE_EDIT);
				}
			} else {
				_shellIconManager.SetFocus(SQSH_INGAME_CHAT_EDIT_ID);
				chatEdit->Show(1);
				chatInfo->setTime(-1);
				chatInfo->Show(1);
				chatEdit->alliesOnlyMode = wantedAlliesMode;
			}
			return;
		} else if (Key.fullkey == (VK_INSERT | KBD_CTRL) || Key.fullkey == (VK_SPACE | KBD_CTRL)) {
			if (chatEdit->isVisible()) {
				if (chatEdit->alliesOnlyMode) {
					chatEdit->alliesOnlyMode = false;
				} else {
					chatEdit->Show(0);
					_shellIconManager.SetFocus(0);
					chatInfo->setTime(CHATINFO_VISIBLE_TIME_AFTER_HIDE_EDIT);
				}
			} else {
				_shellIconManager.SetFocus(SQSH_INGAME_CHAT_EDIT_ID);
				chatEdit->Show(1);
				chatInfo->setTime(-1);
				chatInfo->Show(1);
				chatEdit->alliesOnlyMode = false;
			}
			return;
		} else if (chatEdit->isVisible()) {
			if (Key.fullkey == VK_ESCAPE || (Key.fullkey == VK_RETURN && chatEdit->isEmptyText())) {
				chatEdit->Show(0);
				_shellIconManager.SetFocus(0);
				chatInfo->setTime(CHATINFO_VISIBLE_TIME_AFTER_HIDE_EDIT);
			}
			return;
		}
	}

	switch(Key.fullkey)
	{
		case VK_PAUSE:
			if(!_shellIconManager.isCutSceneMode() && currentSingleProfile.getLastGameType() != UserSingleProfile::MULTIPLAYER) {
				if(!isPaused())
					pauseGame();
				else
					resumeGame();
			}
			return;
            
        //Don't use plain F12 as conflicts with steam screenshotting
		case VK_F12 | KBD_CTRL:
			MakeShot();
			break;

#ifdef PERIMETER_DEBUG
		case VK_F6 | KBD_SHIFT:
			terRenderDevice->StartCaptureFrame();
			break;
#endif
	}

	ControlPressed(Key.fullkey);
}

void GameShell::ControlPressed(uint32_t key)
{
	if (_bMenuMode || isScriptReelEnabled() || _shellIconManager.isCutSceneMode()) {
		return;
	}

    bool handled = true;
    int ctrl = g_controls_converter.key_control(key);
    switch(ctrl)
    {
        default:
            handled = false;
            break;

		case CTRL_CAMERA_MOUSE_LOOK:
			if (!cameraMouseTrack && !cameraMouseShift) {
                cameraMouseTrack = true;
                mousePressControl_ = mousePosition();
                _shellCursorManager.HideCursor();
                SDL_SetRelativeMouseMode(SDL_TRUE);
            }
			break;

		case CTRL_CAMERA_MOUSE_MOVE:
            if (!cameraMouseShift && !cameraMouseTrack) {
                setCameraMouseShift(true);
            }
			break;
	}
    
    if (!handled) {
        handled = hotKeyManager->keyPressed(key);
        if (handled) {
            gameShell->updatePosition();
        }
    }
    
    if (handled) {
        if (lastActivatedControlKey != 0 && lastActivatedControlKey != key) {
            //Release previous key
            ControlUnpressed(lastActivatedControlKey);
        }
        lastActivatedControlKey = key;
    }
}

void GameShell::KeyUnpressed(sKey& Key)
{
    if (terRenderDevice->DebugUIIsEnabled()
    && terRenderDevice->DebugUIKeyPress(&Key, false)) {
        return;
    }

    if (CaptureControlInput && CaptureControlInput(Key.fullkey, false)) {
        return;
    }

	if (_bMenuMode) {
		_shellIconManager.OnKeyUp(Key.fullkey);
		return;
	}

    if (missionEditor_ && missionEditor_->keyUnpressed(Key)) {
        return;
    }

	if (isScriptReelEnabled()) {
 		_shellIconManager.OnKeyDown(Key.fullkey);
		return;
	}

	ControlUnpressed(Key.fullkey);
	
	if (Key.fullkey == VK_SHIFT || Key.fullkey == VK_LSHIFT || Key.fullkey == VK_RSHIFT) {
        bWasShiftUnpressed = true;
    }
}

void GameShell::ControlUnpressed(uint32_t key)
{
	if (_bMenuMode || isScriptReelEnabled()) {
		return;
	}
    
    if (lastActivatedControlKey) {
        //Check if base key is same and was unreleased
        if ((lastActivatedControlKey & VK_MASK) == (key & VK_MASK)) {
            key = lastActivatedControlKey;
        }
        /*
        //If mod key was unpressed disable the control
        uint32_t modflag = getModFlagFromKey(key & VK_MASK);
        if (lastActivatedControlKey & modflag) {
            key = lastActivatedControlKey;
        }
        */

        if (lastActivatedControlKey == key) {
            //Just unset
            lastActivatedControlKey = 0;
        }
    }
    
	int ctrl = g_controls_converter.key_control(key);
    if (ctrl == CTRL_ESCAPE) {
        if(_shellIconManager.IsInterface() && _shellIconManager.interfaceShowFlag()) {
            EnterInMissionMenu();
        }
        return;
    }
    
    if (_shellIconManager.isCutSceneMode()) {
        return;
    }

    bool handled = true;
	switch(ctrl)
	{
        default:
            handled = false;
            break;

        case CTRL_TIME_NORMAL:
            if (currentSingleProfile.getLastGameType() != UserSingleProfile::MULTIPLAYER) {
                _shellIconManager.setNormalSpeed();
            }
            break;
        case CTRL_TIME_DEC:
            if (currentSingleProfile.getLastGameType() != UserSingleProfile::MULTIPLAYER) {
                _shellIconManager.decrSpeedStep();
            }
            break;
        case CTRL_TIME_INC:
            if (currentSingleProfile.getLastGameType() != UserSingleProfile::MULTIPLAYER) {
                _shellIconManager.incrSpeedStep();
            }
            break;
        case CTRL_CAMERA_SAVE1:
        case CTRL_CAMERA_SAVE2:
        case CTRL_CAMERA_SAVE3:
        case CTRL_CAMERA_SAVE4:
        case CTRL_CAMERA_SAVE5:
            terCamera->SaveCamera(ctrl - CTRL_CAMERA_SAVE1);
            break;
        case CTRL_CAMERA_RESTORE1:
        case CTRL_CAMERA_RESTORE2:
        case CTRL_CAMERA_RESTORE3:
        case CTRL_CAMERA_RESTORE4:
        case CTRL_CAMERA_RESTORE5:
            terCamera->RestoreCamera(ctrl - CTRL_CAMERA_RESTORE1);
            break;
		case CTRL_CAMERA_MOUSE_LOOK:
			cancelMouseLook();
			break;
		case CTRL_CAMERA_MOUSE_MOVE:
            setCameraMouseShift(false);
			break;
        case CTRL_CAMERA_TO_EVENT:
#if !defined(ANDROID_XR)
            if (!_shellIconManager.getMiniMapEventIcons().empty()) {
                terCamera->setPosition(_shellIconManager.getMiniMapEventIcons().back().pos);
            }
#endif
            break;
        case CTRL_TOGGLE_MUSIC:
        case CTRL_TOGGLE_SOUND:
            break;
        case CTRL_LOAD:
            prepareForInGameMenu();
            _shellIconManager.SwitchMenuScreens(-1, SQSH_MM_LOAD_IN_GAME_SCR);
            break;
        case CTRL_SAVE:
            prepareForInGameMenu();
            _shellIconManager.SwitchMenuScreens(-1, SQSH_MM_SAVE_GAME_SCR);
            break;
        case CTRL_HOLD_PRODUCTION:
            universe()->toggleHold(true);
            break;
        case CTRL_RESUME_PRODUCTION:
            universe()->toggleHold(false);
            break;
        case CTRL_TOGGLE_LIFEBARS:
            m_ShellDispatcher.toggleAlwaysShowLifebars();
            break;
        case CTRL_TOGGLE_FPS:
            terShowFPS ^= 1;
            break;
	}

    if (handled) {
        return;
    }

    if (_shellIconManager.IsInterface()) {
        if (key >= '0' && key <= '9') {
            if (universe()) {
                universe()->select.selectGroup(key - '0');
            }
            return;
        } else if (key >= ('0' + KBD_CTRL) && key <= ('9' + KBD_CTRL)) {
            if (universe()) {
                universe()->select.putCurrentSelectionToGroup(key - ('0' + KBD_CTRL));
            }
            return;
        } else if (key >= ('0' + KBD_SHIFT) && key <= ('9' + KBD_SHIFT)) {
            if (universe()) {
                universe()->select.addCurrentSelectionToGroup(key - ('0' + KBD_SHIFT));
            }
            return;
        }
    }
}

void GameShell::setCaptureInputCallback(bool (*input_callback)(uint32_t key, bool press)) {
    CaptureControlInput = input_callback;
}

bool GameShell::hasCaptureInputCallback() {
    return CaptureControlInput != nullptr;
}

void GameShell::cancelMouseLook() {
	if(cameraMouseTrack)
	{
		cameraMouseTrack = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
		setCursorPosition(mousePressControl_);
#ifdef __ANDROID__
		// Drop stale relative-mode/warp motion so it cannot move the cursor for a frame.
		SDL_FlushEvent(SDL_MOUSEMOTION);
#endif
        mousePosition_ = mousePressControl_;

		if(_shellIconManager.IsInterface())
			_shellCursorManager.ShowCursor();
	}
}

void GameShell::updatePosition() 
{
	_shellCursorManager.OnMouseMove(mousePosition().x+0.5f, mousePosition().y+0.5f);
    if (BuildingInstallerInited()) {
        BuildingInstaller->SetBuildPosition(Vect2f(mousePosition().x, mousePosition().y), universe()->activePlayer());
    }
}

void GameShell::MouseMove(const Vect2f& pos, const Vect2f& rel)
{
    if (terRenderDevice->DebugUIIsEnabled()
    && terRenderDevice->DebugUIMouseMove(pos)) {
        return;
    }

	if (!_bMenuMode && missionEditor_ && missionEditor_->mouseMove(pos)) {
        return;
    }

	cameraCursorInWindow = true;

#ifdef __ANDROID__
	if (cameraMouseTrack) {
		// Relative motion rotates the camera; keep the visible cursor at the press point.
		mousePositionDelta_ = Vect2f::ZERO;
		mousePositionRelative_ = rel;
		MouseMoveFlag = 1;
	}
	else
#endif
	if(MousePositionLock){
		mousePosition_ = pos;
		mousePositionDelta_ = Vect2f::ZERO;
        mousePositionRelative_ = Vect2f::ZERO;
	}
	else{
        mousePositionDelta_ = pos - mousePosition(); //Do this before setting the current position so we can get Delta
		mousePosition_ = pos;
        mousePositionRelative_ = rel;
		MouseMoveFlag = 1;
	}

	if (BuildingInstallerInited()){
		if(isShiftPressed()) {
            BuildingInstaller->ChangeBuildAngle(mousePositionDelta().y * 50, universe()->activePlayer());
        } else {
            BuildingInstaller->SetBuildPosition(pos, universe()->activePlayer());
        }
	}

	_shellCursorManager.OnMouseMove(mousePosition().x+0.5f, mousePosition().y+0.5f);

	if (!cameraMouseZoom && !cameraMouseShift && !cameraMouseTrack && !toolzerSizeTrack) {
		if(_pShellDispatcher->m_nState != STATE_TRACKING) {
            CursorOverInterface = _shellIconManager.OnMouseMove(mousePosition().x + 0.5f, mousePosition().y + 0.5f);
        }

		m_ShellDispatcher.OnMouseMove(mousePosition().x+0.5f, mousePosition().y+0.5f);
	}
}

#ifdef __ANDROID__
void GameShell::applyAndroidCameraRotation(const Vect2f& rel)
{
    if (!cameraMouseTrack) {
        return;
    }

    if (rel.x == 0.0f && rel.y == 0.0f) {
        return;
    }

    mousePositionRelative_ += rel;
    MouseMoveFlag = 1;
}
#endif

void GameShell::MouseButton(const Vect2f& pos, uint32_t key, bool pressed) {
#ifdef __ANDROID__
    if (pressed && key == VK_MBUTTON) {
        // The synthetic middle-button event must start from the touch midpoint,
        // not from the cursor position left by the previous gesture.
        mousePosition_ = pos;
        mousePositionDelta_ = Vect2f::ZERO;
    }
#endif
    key = sKey(key, true).fullkey;

    if (CaptureControlInput && CaptureControlInput(key, pressed)) {
        return;
    }

	if (!_bMenuMode) {
        if (pressed) {
            ControlPressed(key);
        } else {
            ControlUnpressed(key);
        }
	}
}

void GameShell::MouseLeftPressed(const Vect2f& pos)
{
    uint32_t key = sKey(VK_LBUTTON, true).fullkey;
    if (CaptureControlInput && CaptureControlInput(key, true)) {
        return;
    }

	if (!_bMenuMode && missionEditor_ && missionEditor_->mouseLeftPressed(pos)) {
        return;
    }

	if (universe()) {
        Event ev(Event::MOUSE_CLICK);
		universe()->checkEvent(&ev);
	}

	if (autoSwitchAIEnabled) {
		setActivePlayerAIOff();
	}

	ControlPressed(key);

	if(!mouseLeftPressed())
	{
		mouseLeftPressed_ = true;
		mousePositionDelta_ = pos - mousePosition();
		mousePosition_= pos;

		if(!cameraMouseZoom && !cameraMouseShift && !cameraMouseTrack && !toolzerSizeTrack)
		{
			if(_shellIconManager.IsInterface())
			{
				if(_shellIconManager.OnLButtonDown(mousePosition().x+0.5f, mousePosition().y+0.5f))
					return;

				m_ShellDispatcher.OnLButtonDown(mousePosition().x+0.5f, mousePosition().y+0.5f);
			}
		}
	}
#ifndef __ANDROID__
	if (BuildingInstallerInited()){
		SND2DPlaySound(BuildingInstaller->valid() ? "building_set" : "unable_build");
		BuildingInstaller->ConstructObject(universe()->activePlayer());
	}
#endif
}

void GameShell::MouseRightPressed(const Vect2f& pos)
{
    uint32_t key = sKey(VK_RBUTTON, true).fullkey;
    if (CaptureControlInput && CaptureControlInput(key, true)) {
        return;
    }

	if (!_bMenuMode && missionEditor_ && missionEditor_->mouseRightPressed(pos)) {
	    return;
	}

	if (autoSwitchAIEnabled) {
		setActivePlayerAIOff();
	}

	ControlPressed(key);

	if(!mouseRightPressed())
	{
		mouseRightPressed_ = true;
		mousePositionDelta_ = pos - mousePosition();
		mousePosition_ = pos;

		if (BuildingInstallerInited()) {
            BuildingInstaller->CancelObject();
        }

		//if(!cameraMouseZoom && !cameraMouseShift && !cameraMouseTrack && !toolzerSizeTrack)
		if(!cameraMouseZoom && !cameraMouseShift)
		{
			if(_shellIconManager.IsInterface())
			{
				if(_shellIconManager.OnRButtonDown(mousePosition().x+0.5f, mousePosition().y+0.5f))
					return;
				m_ShellDispatcher.OnRButtonDown(mousePosition().x+0.5f, mousePosition().y+0.5f);
			}
		}
	}
}

void GameShell::MouseLeftUnpressed(const Vect2f& pos)
{
    uint32_t key = sKey(VK_LBUTTON, true).fullkey;
    if (CaptureControlInput && CaptureControlInput(key, false)) {
        return;
    }

	ControlUnpressed(key);

	if (mouseLeftPressed()) {
		mouseLeftPressed_ = false;
		mousePositionDelta_ = pos - mousePosition();
		mousePosition_ = pos;

//		if(BuildingInstaller.inited()){
//			SND2DPlaySound(BuildingInstaller.valid() ? "building_set" : "unable_build");
//			BuildingInstaller.ConstructObject(universe()->activePlayer());
//		}

		if(!cameraMouseZoom && !cameraMouseShift && !cameraMouseTrack && !toolzerSizeTrack && _shellIconManager.IsInterface()){
			if(_pShellDispatcher->m_nState != STATE_TRACKING 
				&& _shellIconManager.OnLButtonUp(mousePosition().x+0.5f, mousePosition().y+0.5f))
					return;
			_shellIconManager.lButtonReset();
			m_ShellDispatcher.OnLButtonUp(mousePosition().x+0.5f, mousePosition().y+0.5f);
		}

#ifdef __ANDROID__
		if (BuildingInstallerInited()) {
			SND2DPlaySound(BuildingInstaller->valid() ? "building_set" : "unable_build");
			BuildingInstaller->ConstructObject(universe()->activePlayer());
		}
#endif
	}
}

void GameShell::MouseRightUnpressed(const Vect2f& pos)
{
    uint32_t key = sKey(VK_RBUTTON, true).fullkey;
    if (CaptureControlInput && CaptureControlInput(key, false)) {
        return;
    }

	ControlUnpressed(key);

	if(mouseRightPressed())
	{
		mouseRightPressed_ = false;
		mousePositionDelta_ = pos - mousePosition();
		mousePosition_ = pos;

		//if(!cameraMouseZoom && !cameraMouseShift && !cameraMouseTrack && !toolzerSizeTrack)
		{
			if(_shellIconManager.IsInterface())
			{
				if(_shellIconManager.OnRButtonUp(mousePosition().x+0.5f, mousePosition().y+0.5f))
					return;
				m_ShellDispatcher.OnRButtonUp(mousePosition().x+0.5f, mousePosition().y+0.5f);
			}
		}
	}
}

void GameShell::MouseWheel(float delta, bool preciseMenuWheel)
{
	if(!_bMenuMode && GameActive && _shellIconManager.IsInterface() && !isScriptReelEnabled()) {
        CChatInfoWindow* chatInfo = (CChatInfoWindow*) _shellIconManager.GetWnd(SQSH_CHAT_INFO_ID);
        if (!chatInfo || !chatInfo->isVisible() || !chatInfo->HitTest(mousePosition().x+0.5f, mousePosition().y+0.5f)) {
            terCamera->mouseWheel(delta);
        }
	}
	if (historyScene->ready()) {
		historyScene->getCamera()->mouseWheel(delta > 0.0f ? 1 : -1);
	}

	if (preciseMenuWheel) {
		_shellIconManager.OnPreciseMouseWheel(delta);
	} else {
		_shellIconManager.OnMouseWheel(delta > 0.0f ? 1 : -1);
	}

	m_ShellDispatcher.OnMouseMove(mousePosition().x+0.5f, mousePosition().y+0.5f);
	_shellCursorManager.OnMouseMove(mousePosition().x+0.5f, mousePosition().y+0.5f);
}
void GameShell::MouseLeave()
{
	cameraCursorInWindow = false;
}
void GameShell::OnWindowActivate()
{
	cameraMouseShift = false;
	cameraMouseTrack = false;
	toolzerSizeTrack = false;
	_shellCursorManager.ShowCursor();
}

void GameShell::MouseLeftDoubleClick(const Vect2f& pos)
{
	mousePositionDelta_ = pos - mousePosition();
	mousePosition_ = pos;

	if(_shellIconManager.IsInterface())
	{
		if (_shellIconManager.OnLButtonDblClk(mousePosition().x+0.5f, mousePosition().y+0.5f))
			return;
		m_ShellDispatcher.OnLButtonDblClk(mousePosition().x+0.5f, mousePosition().y+0.5f);
	}
}

void GameShell::MouseRightDoubleClick(const Vect2f& pos)
{
	mousePositionDelta_ = pos - mousePosition();
	mousePosition_ = pos;

	if(_shellIconManager.IsInterface())
		m_ShellDispatcher.OnRButtonDblClk(mousePosition().x+0.5f, mousePosition().y+0.5f);
}


//----------------------------------

void GameShell::ShotsScan()
{
	shotNumber_ = 0;

    std::string path_str = convert_path_native(terScreenShotsPath);
    create_directories(path_str);

    std::vector<std::string> paths;
    for (const auto & entry : std::filesystem::directory_iterator(std::filesystem::u8path(path_str))) {
        std::string entry_path = entry.path().u8string();
        if (endsWith(entry_path, terScreenShotExt)) {
            const char* p = strstr(entry_path.c_str(), terScreenShotName);
            if(p){
                p += strlen(terScreenShotName);
                if(isdigit(*p)){
                    int t = atoi(p) + 1;
                    if(shotNumber_ < t)
                        shotNumber_ = t;
                }
            }
        }
    }
}

void GameShell::MakeShot()
{
	if(shotNumber_ == -1)
		ShotsScan();
	XBuffer fname;
	fname <= shotNumber_/1000 % 10 <= shotNumber_/100 % 10 <= shotNumber_/10 % 10 <= shotNumber_ % 10 < terScreenShotExt;
	shotNumber_++;
    std::string path = convert_path_native(terScreenShotsPath) + PATH_SEP + terScreenShotName + fname.address();
	terRenderDevice->SetScreenShot(path.c_str());
}

void GameShell::startStopRecordMovie()
{
	recordMovie_ = !recordMovie_;

	if(recordMovie_){
		HTManager::instance()->setSyncroTimer(0, framePeriod_, terMaxTimeInterval);
		frame_time.set(0, framePeriod_, terMaxTimeInterval);
		scale_time.set(0, framePeriod_, terMaxTimeInterval);

		movieShotNumber_ = 0;
		movieStartTime_ = frame_time();
		
        std::string path_str = convert_path_native(terMoviePath);
        if (path_str.empty()) return;
        create_directories(path_str);

        int movieNumber = 0;
        std::vector<std::string> paths;
        for (const auto & entry : std::filesystem::directory_iterator(std::filesystem::u8path(path_str))) {
            std::string entry_path = entry.path().u8string();
            if (startsWith(entry_path, terMovieName)) {
				const char* p = strstr(entry_path.c_str(), terMovieName);
				if(p){
					p += strlen(terMovieName);
					if(isdigit(*p)){
						int t = atoi(p) + 1;
						if(movieNumber < t)
							movieNumber = t;
					}
				}
			}
		}
    
		XBuffer buffer;
		buffer < path_str.c_str() < PATH_SEP < terMovieName <= movieNumber/10 % 10 <= movieNumber % 10;
		movieName_ = buffer;
        create_directories(movieName_);
	} 
	else{
		HTManager::instance()->setSyncroTimer(synchroByClock_, framePeriod_, terMaxTimeInterval);
		frame_time.set(synchroByClock_, framePeriod_, terMaxTimeInterval);
		scale_time.set(synchroByClock_, framePeriod_, terMaxTimeInterval);
	}
}

void GameShell::makeMovieShot()
{
	XBuffer fname;
	fname <= movieShotNumber_/1000 % 10 <= movieShotNumber_/100 % 10 <= movieShotNumber_/10 % 10 <= movieShotNumber_ % 10 < terScreenShotExt;
    shotNumber_++;
    std::string path = movieName_ + PATH_SEP + terMovieFrameName + fname.address();
	movieShotNumber_++;
	terRenderDevice->SetScreenShot(path.c_str());

	terRenderDevice->BeginScene();
	terRenderDevice->SetFont(_pShellDispatcher->getFont());
	XBuffer msg;
	msg.SetDigits(3);
	int time = frame_time() - movieStartTime_;
	int ms = time % 1000;
	msg < "REC: " < fname < " \nTime: " <= time/60000 < ":" <= (time % 60000)/1000 < "." <= ms/100 % 10 <= ms/10 % 10 <= ms % 10;
    sColor4f c(1, 1, 1, 1);
	OutText(20, 20, msg, &c);
	terRenderDevice->SetFont(0);
	terRenderDevice->EndScene();
}


//----------------------------

void GameShell::CameraQuant()
{
#if defined(ANDROID_XR)
    // Keep scripted/follow updates, but the controller's tablet pointer must
    // not also drive legacy mouse navigation or edge scrolling.
    MousePositionLock = 0;
    const float xrDeltaSeconds = frame_time.delta() * 0.001f;
    terCamera->quant(0.0f, 0.0f, xrDeltaSeconds, false);
    terCamera->finishInitialCameraPose();
    MouseMoveFlag = 0;
#else
	if(!cameraMouseTrack && cameraCursorInWindow && !_bMenuMode && 
		!cameraMouseShift && !cameraMouseZoom && !isScriptReelEnabled()){
		//сдвиг когда курсор у края окна
		//if(!CursorOverInterface)
		terCamera->mouseQuant(mousePosition());
	}
	
	MousePositionLock = 0;
	
	//поворот вслед за мышью
	if(cameraMouseTrack && MouseMoveFlag){
		terCamera->tilt(mousePositionRelative_);
		
		MousePositionLock = 1;
		setCursorPosition(Vect2f::ZERO);
	}
	
	//смещение вслед за мышью
	if (cameraMouseShift && MouseMoveFlag) {        
        if (abs(mousePosition_.x) <= 0.5f) {
            terCamera->shift(
                    mapMoveStartCamera_,
                    mapMoveStartCameraPos_,
                    mapMoveStartWorldPos_,
                    mousePosition_
            );
        }
	}

    float delta = frame_time.delta() / 1000.0f;// * PerimeterCameraControlFPS / 1000.0f;
    terCamera->quant(mousePositionDelta().x, mousePositionDelta().y, delta, cameraMouseTrack && MouseMoveFlag);

//	mousePositionDelta_ = Vect2f::ZERO;
	MouseMoveFlag = 0;

	if (!_bMenuMode && !cameraMouseShift && !isScriptReelEnabled()) {
		terCamera->controlQuant();
	}
#endif
}

#if defined(ANDROID_XR)
void GameShell::alignXrCameraToScriptedView(int transitionDurationMs)
{
    const int durationMs = terCamera->initialCameraPosePending()
        ? 0 : transitionDurationMs;
    terCamera->alignXrPositionToScriptedCamera(durationMs);
    xrScriptedCameraAlignmentMs_.store(durationMs);
}
#endif

void terGameShellShowRegionMain()
{
	if(_pShellDispatcher->ShowTerraform())
	{
		MetaRegionLock lock(_pShellDispatcher->regionMetaDispatcher());
		MetaLockRegionDispatcher region=(*_pShellDispatcher->regionMetaDispatcher())[0];
		terExternalRegionShowLineZeroplast(region.data(),gb_RenderDevice->ConvertColor(sColor4c(RegionMain.line_color)));
	}
}

void terGameShellShowRegionMainAlpha()
{
	if(_pShellDispatcher->ShowTerraform())
	{
		MetaRegionLock lock(_pShellDispatcher->regionMetaDispatcher());
		MetaLockRegionDispatcher region=(*_pShellDispatcher->regionMetaDispatcher())[0];
		terExternalRegionShowLineZeroplastVertical(region.data(),gb_RenderDevice->ConvertColor(sColor4c(RegionMain.vertical_color)));
		Column& c=region->getRasterizeColumn();
		terExternalRegionShowColumn(&c,gb_RenderDevice->ConvertColor(sColor4c(RegionMain.area_color)));
	}
}

void terGameShellShowRegionAbyss()
{
	if(_pShellDispatcher->ShowTerraform())
	{
		MetaRegionLock lock(_pShellDispatcher->regionMetaDispatcher());
		MetaLockRegionDispatcher region=(*_pShellDispatcher->regionMetaDispatcher())[1];
		terExternalRegionShowUniform(region.data(),gb_RenderDevice->ConvertColor(sColor4c(255,255,255)));
	}
}

void terGameShellShowEnergy()
{
	MTG();
	if(gameShell->showEnergy())
	{
		terPlayer* player=universe()->activePlayer();
		terUniverse* tu=universe();
		MTAuto lock(universe()->EnergyRegionLocker());
		terExternalRegionShowLine(&player->energyRegion(),gb_RenderDevice->ConvertColor(sColor4c(255,255,255)));
	}
}

//-----------------------------------------------
void CShellLogicDispatcher::init()
{
	int showLifeBars = IniManager("Perimeter.ini", false).getInt("Game","ShowLifeBars");
#ifdef __ANDROID__
	check_command_line_parameter("show_lifebars", showLifeBars);
#endif
	alwaysShowLifeBars = showLifeBars != 0;
	m_pShowExternal[GAME_SHELL_SHOW_REGION_MAIN] = terScene->CreateExternalObj(terGameShellShowRegionMain,RegionMain.texture);
	m_pShowExternal[GAME_SHELL_SHOW_REGION_MAIN_ALPHA] = terScene->CreateExternalObj(terGameShellShowRegionMainAlpha,sRegionTextureEnergy);
	m_pShowExternal[GAME_SHELL_SHOW_REGION_MAIN_ALPHA]->SetSortPass(true);
	m_pShowExternal[GAME_SHELL_SHOW_REGION_ABYSS] = terScene->CreateExternalObj(terGameShellShowRegionAbyss,sRegionTextureAbyss);
	m_pShowExternal[GAME_SHELL_SHOW_ENERGY] = terScene->CreateExternalObj(terGameShellShowEnergy,sRegionTextureEnergy);

	delete pColumnMain;
	pColumnMain=new terRegionColumnMain;

	delete gbCircleShow;
	gbCircleShow=new cCircleShow;
	
	initFonts();

	//камера и сцена для моделей в окошке
	m_hScene = terVisGeneric->CreateScene();

	m_hLight = m_hScene->CreateLight(ATTRLIGHT_DIRECTION);
	m_hLight->SetPosition(MatXf(Mat3f::ID,Vect3f(0,0,0)));
    sColor4f a(0,0,0,1);
    sColor4f b(1,1,1,1);
	m_hLight->SetColor(&a,&b);
	m_hLight->SetDirection(Vect3f(0,0,-1));

	m_hCamera = m_hScene->CreateCamera();
	m_hCamera->SetAttr(ATTRCAMERA_PERSPECTIVE); // перспектива
    m_hCamera->SetAttr(ATTRCAMERA_CLEARZBUFFER);

	MatXf CameraMatrix;
	Identity(CameraMatrix);
	Vect3f CameraPos(0, 0, -1024);
	SetPosition(CameraMatrix,CameraPos,Vect3f(0,0,0));
	SetCameraPosition(m_hCamera, CameraMatrix);
    
    updateSmallCamera();
}

void CShellLogicDispatcher::updateSmallCamera() {
    if (m_hCamera) {
        //float _small_camera_x = small_camera_x*float(terScreenSizeX);
        //float _small_camera_y = small_camera_y*float(terScreenSizeY);
        //float _small_camera_rect_dx  = small_camera_rect_dx*float(terScreenSizeX)/2.f;
        //float _small_camera_rect_dy  = small_camera_rect_dy*float(terScreenSizeY)/2.f;

        //m_hCamera handles the small 3D view of selected stuff
        //clip takes screen relative position but hardcoded positions are relative to 4:3
        //so we just convert X stuff to absolute and then make it relative to screen position
        //this way they are properly positioned/sized in different resolutions

        float renderX = static_cast<float>(terRenderDevice->GetSizeX());
        float _small_camera_x = absoluteUIPosX(small_camera_x, SHELL_ANCHOR_DEFAULT) / renderX;
        float _small_camera_y = small_camera_y;
        float _small_camera_rect_dx  = (absoluteUISizeX(small_camera_rect_dx, SHELL_ANCHOR_DEFAULT) / renderX)/2.f;
        float _small_camera_rect_dy  = small_camera_rect_dy/2.f;

        Vect2f center(_small_camera_x + _small_camera_rect_dx,
                      _small_camera_y + _small_camera_rect_dy);
        sRectangle4f clip(-_small_camera_rect_dx, -_small_camera_rect_dy,
                          _small_camera_rect_dx, _small_camera_rect_dy);
        Vect2f focus(1.0f, 1.0f);
        Vect2f zplane(30.0f, 1e5f);
        m_hCamera->SetFrustum(                          // устанавливается пирамида видимости
                &center,								// центр камеры
                &clip,									// видимая область камеры
                &focus,									// фокус камеры
                &zplane									// ближайший и дальний z-плоскости отсечения
        );
	}
}

void CShellLogicDispatcher::initFonts() {
	_RELEASE(m_hFontUnitsLabel);
	m_hFontUnitsLabel = terVisGeneric->CreateGameFont(sqshShellMainFont1);
}

void CShellLogicDispatcher::close()
{
	for(int i = 0;i < GAME_SHELL_SHOW_MAX;i++){
		if(m_pShowExternal[i])
			m_pShowExternal[i]->Release();
		m_pShowExternal[i] = NULL;
	}

	delete gbCircleShow;
	gbCircleShow=NULL;
	delete pColumnMain;
	pColumnMain=NULL;

	_RELEASE(m_hFontUnitsLabel);

	_RELEASE(m_hModel);
	_RELEASE(m_hCamera);
	_RELEASE(m_hLight);
	_RELEASE(m_hScene);
}

void CShellLogicDispatcher::RegionEndEdit()
{
	XBuffer buffer(8192, true);
    auto regionDispatcher = regionMetaDispatcher();
    regionDispatcher->saveEditing(buffer);
    universe()->activePlayer()->ChangeRegion(buffer);
	buffer.set(0);
    regionDispatcher->loadEditing(buffer);
}

bool CShellLogicDispatcher::ShowTerraform() const
{
	if(universe()->activePlayer()){
		if(m_nEditRegion != editRegionNone)
			return true;
		CSELECT_AUTOLOCK();
		const UnitList& select_list=universe()->select.GetSelectList();
		UnitList::const_iterator ui;
		FOR_EACH(select_list, ui)
		{
			terUnitBase* unit=*ui;
			if(unit->attr()->ID == UNIT_ATTRIBUTE_TERRAIN_MASTER)
				return true;
		}
	}
	return false;
}


//------------------------------------------
void GameShell::initResourceDispatcher()
{
    IniManager perimeter_ini("Perimeter.ini");
	synchroByClock_ = perimeter_ini.getInt("Timer","SynchroByClock");
    int sfr = perimeter_ini.getInt("Timer","StandartFrameRate");
	framePeriod_ = 1000/sfr;

	check_command_line_parameter("synchro_by_clock", synchroByClock_);

	if(const char* s = check_command_line("fps"))
		framePeriod_ = 1000/atoi(s);

	global_time.set(0,terLogicTimePeriod,1000);
	if(check_command_line("synchro_by_clock"))
		synchroByClock_ = 1;
	frame_time.set(synchroByClock_, framePeriod_, terMaxTimeInterval);
	scale_time.set(synchroByClock_, framePeriod_, terMaxTimeInterval);


	///setSpeed(perimeter_ini.getFloat("Game", "GameSpeed"));
}

void GameShell::startResourceDispatcher()
{
	frame_time.adjust();
	scale_time.adjust();

	HTManager::instance()->startSyncroTimer();
}

void GameShell::setSpeed(float d)
{
	MTAuto mtlock(HTManager::instance()->GetLockLogic());
	game_speed = clamp(d, 0, 10);
	HTManager::instance()->setSpeedSyncroTimer(d);
	scale_time.setSpeed(d);
	if (game_speed!=0 && soundPushedByPause) {
		soundPushedByPause = false;
		SNDPausePop();
		xassert(soundPushedPushLevel==SNDGetPushLevel());
		soundPushedPushLevel=INT_MIN;
	} else if (game_speed==0 && !soundPushedByPause) {
		soundPushedByPause = true;
		xassert(soundPushedPushLevel==INT_MIN);
		soundPushedPushLevel=SNDGetPushLevel();
		SNDPausePush();
	}
	_shellIconManager.speedChanged(game_speed);

#ifdef GPX
    if (d < 1) {
        gpx()->sdk4()->interstitialAd();
    }
#endif
}

void GameShell::setWindowClientSize(const Vect2i& size) {
    windowClientSize_ = size;
    setSourceUIResolution(size);
}

void GameShell::setCursorPosition(const Vect2f& pos)
{
    Vect2i ps = convertToScreenAbsolute(pos);
    SDL_WarpMouseInWindow(sdlWindow, (int)ps.x, (int)ps.y);
}

void GameShell::setCursorPosition(const Vect3f& posW)
{
	Vect3f v, e;
	terCamera->GetCamera()->ConvertorWorldToViewPort(&posW, &v, &e);

#if 0
	//TODO is necessary to convert position to absolute like Win32 does with ClientToScreen when windowed?
	if(!terFullScreen)
	{
		e.x *= float(windowClientSize().x)/terRenderDevice->GetSizeX();
		e.y *= float(windowClientSize().y)/terRenderDevice->GetSizeY();
		
		POINT pt = {e.x, e.y};
		::ClientToScreen(hWndVisGeneric, &pt);	
		
		e.x = pt.x; e.y = pt.y;
		
	}
	else
#endif
    {
		e.x *= float(windowClientSize().x)/terRenderDevice->GetSizeX();
		e.y *= float(windowClientSize().y)/terRenderDevice->GetSizeY();
	}

    SDL_WarpMouseInWindow(sdlWindow, (int)e.x, (int)e.y);
}

void GameShell::setSideArrowsVisible(bool visible) {
	_shellCursorManager.m_bShowSideArrows = visible ? 1 : 0;
}

void GameShell::rememberPlayerCamera(terPlayer* player, const char* triggerName)
{
	if(universe()->activePlayer()->isWorld())
		return;

    savePrm().manualData.saveCamera(player->playerStrategyIndex(), triggerName);
}

void GameShell::setCountDownTime(int timeLeft) {
	countDownTimeMillisLeft = timeLeft;
}

const std::string& GameShell::getCountDownTime() {
	if (countDownTimeMillisLeft != countDownTimeMillisLeftVisible) {
		countDownTimeMillisLeftVisible = countDownTimeMillisLeft;
		countDownTimeLeft = formatTimeWithoutHour(countDownTimeMillisLeftVisible);
	}
	return countDownTimeLeft;
}

std::string GameShell::getTotalTime() const {
	return formatTimeWithHour(gameTimer());
}

//Autoswitch AI
void GameShell::checkAutoswitchAI() {
	autoSwitchAITimer += frame_time.delta();
	if (
			autoSwitchAITimer > 1000 * 60
		&&	universe()
		&&	universe()->activePlayer()
		&&	!universe()->activePlayer()->isAI() ) {
		
		autoSwitchAITimer = 0;
		universe()->activePlayer()->setAI(true);
	}
}

void GameShell::setActivePlayerAIOff() {
	autoSwitchAITimer = 0;
	if (
			universe()
		&&	universe()->activePlayer()
		&&	universe()->activePlayer()->isAI() ) {
		
		universe()->activePlayer()->setAI(false);
	}
}

void GameShell::changeControlState(const std::vector<SaveControlData>& newControlStates, bool reset_controls) {
	_shellIconManager.changeControlState(newControlStates, reset_controls);
}

void GameShell::fillControlState(std::vector<SaveControlData>& controlStatesToSave) {
	_shellIconManager.fillControlState(controlStatesToSave);
}

void GameShell::setScriptReelEnabled(bool isScriptReelEnabled) {
	scriptReelEnabled = isScriptReelEnabled;
/*
	if (scriptReelEnabled) {
		_shellCursorManager.HideCursor();
	} else {
		_shellCursorManager.ShowCursor();
	}
*/
	_shellCursorManager.m_bShowSideArrows = !isScriptReelEnabled;
	CShellWindow* wnd = _shellIconManager.GetWnd(SQSH_EMPTY_WND);
	if (wnd) {
		wnd->Show(scriptReelEnabled);
	}
}

void GameShell::updateMap() {
	if (terMapPoint) {
		terMapPoint->UpdateMap(Vect2i(0,0), Vect2i((int)vMap.H_SIZE-1,(int)vMap.V_SIZE-1) );
	}
}

extern bool isTrueFullscreen();
extern void PerimeterSetupDisplayMode();

void GameShell::updateResolution(bool change_depth, bool change_size, bool change_display_mode) {
    if (change_display_mode) {
        PerimeterSetupDisplayMode();
    }
    
	int mode = RENDERDEVICE_MODE_RETURNERROR;
	if (!isTrueFullscreen()) {
        mode |= RENDERDEVICE_MODE_WINDOW;
    }
	if (terBitPerPixel==16) {
        mode |= RENDERDEVICE_MODE_RGB16;
    } else {
        mode |= RENDERDEVICE_MODE_RGB32;
    }
    if (terVSyncEnable) {
        mode |= RENDERDEVICE_MODE_VSYNC;
    }

	if(!terRenderDevice->ChangeSize(
		terScreenSizeX,
		terScreenSizeY,
		mode
	))
	{
		ErrorInitialize3D();
	}


	if( terRenderDevice->IsFullScreen() && gameShell) {
		gameShell->setWindowClientSize(Vect2i(terScreenSizeX, terScreenSizeY));
	}

    if(change_size) {
        setSourceUIResolution(Vect2i(terScreenSizeX, terScreenSizeY));
		historyScene->onResolutionChanged();
		bwScene->onResolutionChanged();
		bgScene->onResolutionChanged();
		_shellIconManager.onSizeChanged();
		terVisGeneric->ReloadAllFont();
	}

	if(change_depth)
	{
		GetTexLibrary()->ReloadAllTexture();
	}
}

void GameShell::serverMessage(const LocalizedText* text) {
    _shellIconManager.showHintChat(text, 5000);
}

void GameShell::showReelModal(const char* videoFileName, const char* soundFileName, bool localized, bool stopBGMusic, int alpha) {
	std::string path;
	if (localized) {
		path = getLocDataPath() + std::string("Video\\") + videoFileName;
	} else {
		path = videoFileName;
	}
    std::string original_extension = string_to_lower(getExtension(path, false).c_str());
    bool not_found = get_content_entry(path) == nullptr;
    if (original_extension.empty() || not_found) {
        if (!original_extension.empty() && not_found) {
            //No file found, remove extension for auto finding other extension files
            path = setExtension(path, nullptr);
        }
        //Attempt to find extension, bik must be last as is the default one
        for (const auto& ext : { ".mkv", ".bik" }) {
            if (!original_extension.empty() && original_extension == ext) {
                continue;
            }
            if (get_content_entry(path + ext)) {
                path += ext;
                break;
            }
        }
    }
	reelManager.showModal(path.c_str(), soundFileName, stopBGMusic, alpha);
	if (stopBGMusic) {
		PlayMusic();
	}
}

void GameShell::showPictureModal(const char* pictureFileName, bool localized, int stableTime) {
	std::string path;
	if (localized) {
		path = getLocDataPath() + std::string("Video\\") + pictureFileName;
	} else {
		path = pictureFileName;
	}
	reelManager.showPictureModal(path.c_str(), stableTime);
}

void GameShell::setCutSceneMode(bool on, bool animated) {
	_shellIconManager.setCutSceneModeSafe(on, animated);
}

bool GameShell::isCutSceneMode() {
	return _shellIconManager.isCutSceneMode();
}

void GameShell::prepareForInGameMenu() {
	if (cameraMouseTrack) {
		cameraMouseTrack = false;
		setCursorPosition(mousePressControl_);

		if(_shellIconManager.IsInterface())
			_shellCursorManager.ShowCursor();
	}
    setCameraMouseShift(false);
	CancelEditWorkarea();
	_shellCursorManager.m_bShowSideArrows=0;
	_shellCursorManager.ShowCursor();
	_bMenuMode = 1;

	if (currentSingleProfile.getLastGameType() != UserSingleProfile::MULTIPLAYER) {
		pauseGame(true);
	}
}

void GameShell::switchActivePlayer(bool next) {
	MTAuto lock(HTManager::instance()->GetLockLogic());
	activePlayerID_ += next ? 1 : -1;
	int max = (universe()->Players.size() - 1);
	if (activePlayerID_ >= max) {
		activePlayerID_ = 0;
	} else if (activePlayerID_ < 0) {
		activePlayerID_ = max - 1;
	}
	universe()->SetActivePlayer(activePlayerID_);
}

void GameShell::setLocalizedFontSizes() {
	std::string path = getLocDataPath() + "Fonts\\Font.ini";
	IniManager ini = IniManager(path.c_str());

	shell_main_menu_font_size1 = ini.getInt("Sizes","Menu1");
	shell_main_menu_font_size1_5 = ini.getInt("Sizes","Menu2");
	shell_main_menu_font_size2 = ini.getInt("Sizes","Menu3");
	shell_main_menu_font_size3 = ini.getInt("Sizes","Menu4");
	shell_main_menu_font_size4 = ini.getInt("Sizes","Menu5");

	HISTORY_SCENE_LOG_FONT_SIZE = ini.getInt("Sizes","ChainLog");

	defaultFontSize = ini.getInt("Sizes","Default");
	comboBoxFontSize = ini.getInt("Sizes","ComboBox");
	editBoxFontSize = ini.getInt("Sizes","EditBox");

	sqshCursorWorkAreaSize = ini.getInt("Sizes","WorkAreaCursor");

	sqshFontCountDownTimeSize = ini.getInt("Sizes","CountDownTimer");
	infoWndFontSize = ini.getInt("Sizes","InfoWnd");
	HINT_FONT_SIZE = ini.getInt("Sizes","CutSceneHint");

	inGameButtonFontSize = ini.getInt("Sizes","InGameButton");
	collectedEnergyBarFontSize = ini.getInt("Sizes","CollectedEnergyBar");

	statsTableFontSize = ini.getInt("Sizes","StatsTable");
	statsHeadTableFontSize = ini.getInt("Sizes","StatsHeadTable");
}

void GameShell::preLoad() {
    g_controls_converter.LoadCtrlTable();

    const std::string& locale = getLocale();
    if (get_content_entry("RESOURCE/scenario_" + locale + ".hst")) {
        historyScene->loadProgram("RESOURCE/scenario_" + locale + ".hst");
    } else {
        historyScene->loadProgram("RESOURCE/scenario.hst");
    }
    bwScene->loadProgram("RESOURCE/menu.hst");

    std::string path = getLocDataPath() + "Text";
    qdTextDB& texts = qdTextDB::instance();
    texts.clear();
    texts.load_from_directory(locale, path, true);

    //Iterate each mod Text folder for current locale and parse all files
    for (const auto& pair : getGameMods()) {
        if (!pair.second.enabled) continue;
        texts.load_from_directory(locale, pair.second.path + PATH_SEP + getLocDataPath() + "Text", false);
    }
    
    //Export text if desired
    const char* export_texts = check_command_line("export_texts");
    if (export_texts) {
        texts.exportTexts(export_texts);
        fprintf(stdout, "Texts exported %s\n", export_texts);
        ErrH.Exit();
    }
    
    //Load the builtin texts that might not be provided by mods
    texts.load_supplementary_texts(getLocale());
    texts.load_replacement_texts(getLocale());

    //Preload key name strings
    g_controls_converter.LoadKeyNames();
    
    //Setup initial menu
    const char* initial_menu_str = check_command_line("initial_menu");
    if (initial_menu_str) {
        std::string initial_menu = std::string("SQSH_MM_") + initial_menu_str + "_SCR";
        int id = getEnumDescriptor(SQSH_MM_START_SCR)->keyByName(initial_menu.c_str());
        if (id < SQSH_MM_START_SCR || id >= SQSH_MM_SCREENS_MAX) {
            fprintf(stderr, "Initial menu %s not found\n", initial_menu.c_str());
        } else {
            _shellIconManager.initialMenu = static_cast<ShellControlID>(id);
        }
    }
}

void GameShell::setSkipCutScene(bool skip) 
{ 
	cutSceneSkipped_ = skip; 
	if(skip) 
		lastSkipTime_.start(); 
}

void GameShell::recreateChaos() {
	if (chaos) {
		createChaos();
	}
}
void GameShell::createChaos() {
	if (chaos) {
		delete chaos;
		chaos= nullptr;
	}

	chaos = new CChaos(vMap.H_SIZE,vMap.V_SIZE,terEnableBumpChaos);
}

void GameShell::editParameters()
{
	terRenderDevice->Flush(true);
    SDL_ShowCursor(SDL_TRUE);

	bool reloadParameters = false;
	savePrm().manualData.zeroLayerHeight = vMap.hZeroPlast;
    
    bool russian = startsWith(getLocale(), "russian");
	const char* header = russian ? "Заголовок миссии" : "Mission header";
	const char* mission = russian ? "Миссия" : "Mission";
	const char* missionAll = russian ? "Миссия все данные" : "Mission all data";
	const char* debugPrm = "Debug.prm";
	const char* global = russian ? "Глобальные параметры" : "Global parameters";
	const char* attribute = russian ? "Атрибуты" : "Attributes";
	const char* sounds = russian ? "Звуки" : "Sounds";
	const char* interface_ = russian ? "Интерфейс" : "Interface";
	const char* physics = russian ? "Физические параметры" : "Physics parameters";
    const char* separator = "--------------";

	std::vector<const char*> items;
	items.push_back(header);
	items.push_back(mission);
	items.push_back(separator);
	items.push_back(global);
	items.push_back(attribute);
	items.push_back(sounds);
	items.push_back(interface_);
	items.push_back(physics);
	items.push_back(separator);
	items.push_back(missionAll);
	items.push_back(debugPrm);

	const char* item = popupMenu(items);
	if(!item)
		return;
	else if(item == header){
		if(EditArchive().edit(CurrentMission)){
			SavePrm data;
			CurrentMission.loadMission(data);
			CurrentMission.saveMission(data, false);
		}
	}
	else if(item == mission){
		if(EditArchive().edit(savePrm().manualData, CurrentMission.savePathContent().c_str())){
			SavePrm data;
			CurrentMission.loadMission(data);
			data.manualData = manualData();
			CurrentMission.saveMission(data, false);
		}
	}
	else if(item == missionAll){
		if(EditArchive().edit(savePrm(), CurrentMission.savePathContent().c_str())){
			SavePrm data = savePrm();
			CurrentMission.saveMission(data, false);
		}
	}
	else if(item == debugPrm){
		debugPrm_.edit();
	}
	else if(item == attribute){
        EditArchive ea = EditArchive();
		attributeLibrary.edit(ea);
	}
	else if(item == global){
        EditArchive ea = EditArchive();
		globalAttr.edit(ea);
	}
	else if(item == sounds){
        EditArchive ea = EditArchive();
		soundScriptTable.edit(ea);
	}
	else if(item == interface_){
        EditArchive ea = EditArchive();
		interfaceAttr.edit(ea);
	}
	else if(item == physics){
        EditArchive ea = EditArchive();
		rigidBodyPrmLibrary.edit(ea);
	}

	if(manualData().zeroLayerHeight != vMap.hZeroPlast)
		IniManager(GetTargetName(vMap.worldIniFile).c_str()).putInt("Global Parameters", "hZeroPlast", manualData().zeroLayerHeight);

	if(reloadParameters){
		if(universe())
			universe()->RefreshAttribute();
		_shellIconManager.LoadControlsGroup(SHELL_LOAD_GROUP_GAME, true);
	}

	terCamera->setFocus(HardwareCameraFocus);
    SDL_ShowCursor(SDL_FALSE);
	RestoreFocus();
}

void GameShell::setCameraMouseShift(bool _cameraMouseShift) {
    if (_cameraMouseShift) {
        if (terCameraType::cursorTraceCameraDragPlane(
                terCamera->GetCamera(),
                mousePosition_,
                &mapMoveStartWorldPos_
        )) {
            terCamera->GetCamera()->SetCopy(mapMoveStartCamera_);
            mapMoveStartCameraPos_ = terCamera->coordinate().position();
        } else {
            //Couldn't pick raytrace, abort
            _cameraMouseShift = false;
        }
    }
    if (cameraMouseShift == _cameraMouseShift) {
        return;
    }

    cameraMouseShift = _cameraMouseShift;

    if (cameraMouseShift){
        _shellCursorManager.HideCursor();
    } else {
        if (_shellIconManager.IsInterface()) {
            _shellCursorManager.ShowCursor();
        }
    }
}
