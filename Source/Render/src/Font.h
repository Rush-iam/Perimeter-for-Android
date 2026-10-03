#ifndef _FONT_H_
#define _FONT_H_
#pragma once

class cFontInternal : public cUnknownClass
{
public:
	cFontInternal(int kind=KIND_FONT);
	virtual ~cFontInternal();

	bool Create(const std::string& root_dir, const std::string& locale, const std::string& fname, int h, bool silentErr=false);
	
	virtual cTexture* GetTexture()		{return pTexture;};
	virtual float GetHeight()			{
        float height = FontHeight*(GetTexture() ? GetTexture()->GetHeight() : 1);
#if defined(ANDROID_XR)
        height /= rasterScale;
#endif
        return height;
    }
#if defined(ANDROID_XR)
    unsigned GetRasterScale() const { return rasterScale; }
#endif
	bool Reload(const char* root_dir);

	std::vector<Vect3f>		Font; // x,y - position, z - font width
	float				FontHeight;

	std::string font_name;
    std::string locale;
	int GetStatementHeight() const {return statement_height;};
protected:
	int statement_height;
#if defined(ANDROID_XR)
    unsigned rasterScale = 1;
#endif
	cTexture* pTexture;
	friend class cFont;
	
	bool CreateTexture(const char* fontname, const char* fname, int height);
	bool CreateImage(const char* filename, const char* fontname, int height, class cFontImage* image);
	bool Save(const char* fname,cFontImage& fnt);
	bool Load(const char* fname,cFontImage& fnt);
};

class cFont : public cUnknownClass
{
public:
	cFont(cFontInternal* p):cUnknownClass(KIND_FONT),pFont(p),Scale(1,1){};
	virtual ~cFont(){};

	virtual cTexture* GetTexture(){return pFont->GetTexture(); };
	virtual float GetHeight()	  {return pFont->GetHeight(); }

	virtual Vect2f GetScale() const     { return Scale; }
	virtual void SetScale(const Vect2f& scale){Scale=scale; }
    Vect2f GetTextureScale() const {
        const Vect2f scale = GetScale();
#if defined(ANDROID_XR)
        // Texture density is independent of the UI's logical font scale.
        return Vect2f(scale.x / pFont->GetRasterScale(), scale.y / pFont->GetRasterScale());
#else
        return scale;
#endif
    }

	inline cFontInternal* GetInternal(){return pFont;}
protected:
	Vect2f Scale;
	cFontInternal* pFont;
};


#endif //_FONT_H_