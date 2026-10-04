#pragma once

#ifndef _BGSCENE_H
#define _BGSCENE_H

#if defined(ANDROID_XR)
class XrCameraRig;
struct AndroidXrEyeView;
struct AndroidXrInputFrame;
#endif

class BGScene {

	public:

		BGScene();
		~BGScene();

		void init(cVisGeneric* visGeneric);
		void done();
		bool ready() const;

		void quant(float dt);
		void preDraw();
		void draw();
		void postDraw();
#if defined(ANDROID_XR)
		void prepareXrViews(const XrCameraRig& rig, const AndroidXrEyeView views[2]);
		void drawXrView(unsigned eye);
		struct XrMenuHit {
			bool valid = false;
			Vect2f uiPosition = Vect2f(0.0f, 0.0f); // Normalized authored UI coordinates.
			float distanceMeters = 0.0f;
		};
		void hitXrMenu(const XrCameraRig& rig, const AndroidXrInputFrame& input,
		               XrMenuHit (&hits)[2]);
		cCamera* xrCamera(unsigned eye) const { return xrEyes[eye]; }
		float xrMenuPanelDistanceUnits() const { return menuPanelDistanceUnits; }
		float xrMenuPanelWidthUnits() const { return menuPanelWidthUnits; }
#endif

		void reset();

		void setProgress(float progress);

		bool isPlaying() const;
		void play();
		void markToPlay(const char* objName, const char* chainName, bool forward);
		void markAllToPlay(bool forward);
		void unmarkToPlay(const char* objName, const char* chainName, bool forward);

		void onResolutionChanged();

		void setEnabled(bool state) {
			enabled = state;
		}

		void setSkinColor(const sColor4f& color = sColor4f(1, 1, 1, 1)) {
			if ((!(color == skinColor)) && bgObj) {
				skinColor = color;
				bgObj->SetSkinColor(color);
			}
		}

		bool inited() const {
			return (scene != 0);
		}

	protected:

		cUnkLight* light = nullptr;
		cScene* scene = nullptr;
		cCamera* camera = nullptr;
#if defined(ANDROID_XR)
		cCamera* xrEyes[2] = {};
		float menuPanelDistanceUnits = 0.0f;
		float menuPanelWidthUnits = 0.0f;
#endif

		cObjectNodeRoot	*bgObj = nullptr;

		float timer = 0.0f;
		float liveTimer = 0.0f;

		bool enabled = false;

		struct SubObject {
			cObjectNode* node;
			cObjectNode* liveGroupNode;
			bool stopped;
			bool forwardDirection;
			std::string chainName;
		};

		std::vector<SubObject> subObjects = {};

		sColor4f skinColor = {};
};

#endif //_BGSCENE_H
