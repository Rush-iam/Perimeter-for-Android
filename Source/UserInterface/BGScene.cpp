#include "StdAfx.h"
#include "Umath.h"
#include "IRenderDevice.h"
#include "IVisGeneric.h"
#include "BGScene.h"
#include "GameShellSq.h"
#include "SourceUIResolution.h"
#if defined(ANDROID_XR)
#include "xr/XrSceneCamera.h"
#include "AnimChannelNode.h"
#include "xr/XrUnitRay.h"
#endif

BGScene::BGScene() {
	scene = 0;
	camera = 0;
	light = 0;
	bgObj = 0;
	timer = 0;
	liveTimer = 0;
	enabled = true;
}

BGScene::~BGScene() {
	done();
}

void BGScene::init(cVisGeneric* visGeneric) {
	if (ready()) {
		done();
	}
	scene = visGeneric->CreateScene();

	camera = scene->CreateCamera();
	camera->SetAttr(ATTRCAMERA_PERSPECTIVE);
	camera->SetAttr(ATTRCAMERA_CLEARZBUFFER);
	Vect3f pos;
	pos.setSpherical(bgCameraPsi, bgCameraTheta, bgCameraDist);
	pos += Vect3f(bgCameraX, bgCameraY, bgCameraZ);
#if defined(ANDROID_XR)
	menuPanelDistanceUnits = pos.norm();
#endif
	onResolutionChanged();

	MatXf matrix = MatXf::ID;
	matrix.rot() = Mat3f(bgCameraTheta, X_AXIS) * Mat3f(XM_PI / 2 - bgCameraPsi, Z_AXIS);
	matrix *= MatXf(Mat3f::ID, -pos);	

	MatXf ml = MatXf::ID;
	ml.rot()[2][2] = -1;
	ml.rot()[1][1] = -1;
	MatXf cameraMatrix;
	cameraMatrix = ml * matrix;

	camera->SetPosition(cameraMatrix);

	
	light = scene->CreateLight(ATTRLIGHT_DIRECTION);
	light->SetPosition( MatXf(Mat3f::ID, Vect3f(0, 0, 0)) );
    sColor4f a(1, 1, 1, 1);
    sColor4f b(1, 1, 1, 1);
	light->SetColor( &a, &b );
	light->SetDirection( Vect3f(bgLightX, bgLightY, bgLightZ) );

	bgObj = scene->CreateObject("RESOURCE\\Models\\Menu\\interface.M3D", NULL);
	setSkinColor();
#if defined(ANDROID_XR)
	// Plane01 is the finite dark filter. Force just this mesh hidden across all
	// of its visibility channels; the other animated menu meshes stay intact.
	if (bgObj) {
		if (cObjectNode* shadePlane = bgObj->FindObject("Plane01")) {
			if (cAnimChannelNode* animation = shadePlane->GetAnimChannel()) {
				for (int channel = 0; channel < animation->GetNumberChannel(); ++channel) {
					cAnimChainNode* chain = animation->GetChannel(channel);
					for (int key = 0; key < chain->GetNumberVisible(); ++key)
						chain->GetVisible(key).visible = 0;
				}
			}
		}
	}
#endif
}

void BGScene::onResolutionChanged() {
	if (camera) {
        Vect2f center(0.5f,0.5f);
        float x = 0.5f;
        //x *= (1.0f / static_cast<float>(source_ui_factor.x));
        sRectangle4f clip(-x,-0.5f,x,0.5f);
        //This keeps aspect ratio fixed on Y axis
        float f = MAIN_MENU_RATIO / getRenderRatio();
        Vect2f focus(f, f);
#if defined(ANDROID_XR)
		menuPanelWidthUnits = menuPanelDistanceUnits / focus.x;
#endif
        Vect2f zplane(10.0f, 1e5f);
        camera->SetFrustum(
                &center,								// центр камеры
                &clip,									// видимая область камеры
                &focus,									// фокус камеры
                &zplane									// ближайший и дальний z-плоскости отсечения
        );
	}
}

void BGScene::done() {
	reset();
#if defined(ANDROID_XR)
	for (auto& eye : xrEyes) RELEASE(eye);
#endif
	RELEASE(light);
	RELEASE(camera);
	RELEASE(bgObj);
	RELEASE(scene);
}

bool BGScene::ready() const {
	return (enabled && inited());
}

void BGScene::quant(float dt) {
	if (timer != 0) {
		timer -=dt;
		if (timer <= 0) {
			timer = 0;
		}
		for (int i = 0, s = subObjects.size(); i < s; i++) {
			if (!subObjects[i].stopped) {
				subObjects[i].node->SetPhase( subObjects[i].forwardDirection ? (1.0f - timer / bgEffectTime) : (timer / bgEffectTime), false );
			}
		}
		if (timer == 0) {
			std::vector<SubObject>::iterator it = subObjects.begin();
			while (it != subObjects.end()) {
				if ((*it).forwardDirection) {
					(*it).stopped = true;
					it++;
				} else {
					if (!(*it).stopped) {
						it = subObjects.erase(it);
					} else {
						it++;
					}
				}
			}
		}
	}

	if (liveTimer != 0) {
		liveTimer -=dt;
		if (liveTimer< 0) {
			liveTimer= 0;
		}
		for (int i = 0, s = subObjects.size(); i < s; i++) {
			if (subObjects[i].liveGroupNode) {
				subObjects[i].liveGroupNode->SetPhase( 1.0f - liveTimer / bgEffectTime, false );
			}
		}
	}
	if (liveTimer == 0) {
		liveTimer = bgEffectTime;
	}

	scene->dSetTime(dt);
}

void BGScene::markAllToPlay(bool forward) {
	for (int i = 0, s = subObjects.size(); i < s; i++) {
		subObjects[i].stopped = false;
		subObjects[i].forwardDirection = forward;
	}
}

void BGScene::unmarkToPlay(const char* objName, const char* chainName, bool forward) {
	int i;
	int s;
	for (i = 0, s = subObjects.size(); i < s; i++) {
		if ( 
				!strcmp(objName, subObjects[i].node->GetName())
			&&	subObjects[i].chainName == chainName) {

			break;
		}
	}
	if (i != s) {
		subObjects[i].stopped = true;
		subObjects[i].forwardDirection = forward;
	}
}

void BGScene::markToPlay(const char* objName, const char* chainName, bool forward) {
	int i;
	int s;
	for (i = 0, s = subObjects.size(); i < s; i++) {
		if ( 
				!strcmp(objName, subObjects[i].node->GetName())
			&&	subObjects[i].chainName == chainName) {

			break;
		}
	}
	if (i == s) {
		subObjects.push_back(SubObject());
		subObjects.back().stopped = false;
		subObjects.back().forwardDirection = forward;
		subObjects.back().node = bgObj->FindObject(objName);
		subObjects.back().liveGroupNode = subObjects.back().node->FindObject("group live");
		if (subObjects.back().liveGroupNode) {
			subObjects.back().liveGroupNode->SetChannel("live", false);
		}
		subObjects.back().chainName = chainName;
		subObjects.back().node->SetChannel(chainName, false);
	} else if (forward != subObjects[i].forwardDirection) {
		subObjects[i].stopped = false;
		subObjects[i].forwardDirection = forward;
	}
}

bool BGScene::isPlaying() const {
	return (timer != 0);
}

void BGScene::play() {
	for (int i = 0, s = subObjects.size(); i < s; i++) {
		if (!subObjects[i].stopped) {
			timer = bgEffectTime;
			return;
		}
	}
}

void BGScene::reset() {
	subObjects.clear();
	timer = 0;
}

void BGScene::preDraw() {
	scene->PreDraw(camera);
}

void BGScene::draw() {
	scene->Draw(camera);
}

void BGScene::postDraw() {
	scene->PostDraw(camera);
}

#if defined(ANDROID_XR)
void BGScene::hitXrMenu(const XrCameraRig& rig, const AndroidXrInputFrame& input,
                        XrMenuHit (&hits)[2]) {
    for (auto& hit : hits) hit = {};
    if (!ready() || !bgObj || !camera) return;
    MatXf centerWorld = camera->GetMatrix();
    centerWorld.invert();
    XrWorldRay rays[2];
    const float limit = camera->GetZPlane().y;
    for (unsigned hand = 0; hand < 2; ++hand) {
        if (!input.hands[hand].aimValid) continue;
        const MatXf aim = centerWorld * rig.Pose(input.hands[hand].aimPosition,
                                                input.hands[hand].aimOrientation);
        Vect3f direction = aim.rot() * Vect3f::K;
        direction.normalize();
        rays[hand] = {aim.trans(), direction, limit};
    }
    xrIntersectUnitRays(*bgObj, rays);
    for (unsigned hand = 0; hand < 2; ++hand) {
        if (rays[hand].distance <= 0.0f || rays[hand].distance >= limit) continue;
        const Vect3f point = camera->GetMatrix() *
            (rays[hand].origin + rays[hand].direction * rays[hand].distance);
        if (point.z <= 0.0f) continue;
        // Invert the authored menu projection to preserve its existing UI regions.
        const float x = camera->GetCenterX() +
            point.x * camera->GetScaleViewPort().x * camera->GetFocusX() / point.z;
        const float y = camera->GetCenterY() -
            point.y * camera->GetScaleViewPort().y * camera->GetFocusY() / point.z;
        if (!std::isfinite(x) || !std::isfinite(y) ||
            x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) continue;
        hits[hand] = {true, Vect2f(x, y), rays[hand].distance / rig.UnitsPerMeter()};
    }
}

void BGScene::prepareXrViews(const XrCameraRig& rig, const AndroidXrEyeView views[2]) {
	xrPrepareSceneCameras(scene, camera, xrEyes, rig, views);
	scene->PreDraw(camera);
	scene->PrepareViewFamily();
}

void BGScene::drawXrView(unsigned eye) {
	scene->DrawView(xrEyes[eye]);
}
#endif

void BGScene::setProgress(float progress) {
	cObjectNode* node = bgObj->FindObject("group progress");
	node->SetChannel("progress", false);
	node->SetPhase( progress, false );
}
