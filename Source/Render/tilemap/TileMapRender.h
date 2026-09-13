#ifndef PERIMETER_TILEMAPRENDER_H
#define PERIMETER_TILEMAPRENDER_H

#include <array>

struct sBumpTile;
class cTilemapTexturePool;
struct VectDelta;

class cTileMapRender
{
private:
    cTileMap* tilemap;
    std::vector<sBumpTile*> bumpTiles;
    std::vector<int> bumpDyingTiles;
    // Zoom LOD cache: retain at most two inactive LOD variants per
    // tile so a reversing zoom can restore prepared geometry instead of
    // rebuilding it. The cache uses shared tilemap resources and works with
    // both Android renderers; the Android launch option controls whether it
    // is enabled.
    std::vector<std::array<int, 2>> bumpTileLodCache;
    bool lodCacheEnabled = false;
    std::vector<cTilemapTexturePool*> bumpTexPools;
    class VertexPoolManager* vertexPoolManager = nullptr;
    class IndexPoolManager* indexPoolManager = nullptr;
    IndexBuffer tilemapIB;
    int index_offset[TILEMAP_LOD];
    int index_size[TILEMAP_LOD];

    uint8_t* visMap;
    char* vis_lod;

    char* update_stat;
    bool update_in_frame;

    void SaveUpdateStat();

    void cacheBumpTile(int tileIndex, int id);
    int takeCachedBumpTile(int tileIndex, int lod);
    void discardCachedBumpTiles(int tileIndex);

    VectDelta* delta_buffer;
    std::vector<std::vector<sPolygon>> index_buffer;
    
    cTilemapTexturePool* FindFreeTexturePool(int tex_width, int tex_height);
public:
    void IncUpdate(sBumpTile* pbump);

    explicit cTileMapRender(cTileMap *pTileMap);
    ~cTileMapRender();

    VertexPoolManager* GetVertexPool() { return vertexPoolManager; }
    IndexPoolManager* GetIndexPool() { return indexPoolManager; }
    
    void ClearTilemapPool();
    void RestoreTilemapPool();

    int bumpNumIndex(int lod){return index_size[lod];};
    int bumpIndexOffset(int lod){return index_offset[lod];};

    int bumpNumVertices(int lod);
    void bumpCreateIB(sPolygon* ib, int lod);
    int bumpTileValid(int id);
    int bumpTileAlloc(int lod,int xpos,int ypos,sBumpTile* textureSource = NULL);
    void bumpTileFree(int id);
    void bumpTilesDeath();
    void CalcTileMap(cCamera* DrawNode);

    void PreDraw(cCamera* DrawNode);
    void DrawBump(cCamera* DrawNode,eBlendMode MatMode,TILEMAP_DRAW tile_draw,bool shadow);

    sBumpTile* GetTile(int k,int n)
    {
        VISASSERT(k>=0 && k < tilemap->GetTileNumber().x);
        VISASSERT(n>=0 && n < tilemap->GetTileNumber().y);
        int bumpTileID = tilemap->GetTile(k, n).bumpTileID;
        if(bumpTileID>=0)
            return bumpTiles[bumpTileID];
        return NULL;
    }

    VectDelta* GetDeltaBuffer(){return delta_buffer;};
    std::vector<std::vector<sPolygon> >& GetIndexBuffer(){return index_buffer;};
};

#endif //PERIMETER_TILEMAPRENDER_H
