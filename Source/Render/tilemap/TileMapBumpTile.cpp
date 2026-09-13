#include <climits>
#include "StdAfxRD.h"
#include "VertexFormat.h"
#include "PoolManager.h"
#include "../Game/Region.h"
#include "TileMap.h"
#include "TileMapRender.h"
#include "TileMapBumpTile.h"
#include "TileMapTexturePool.h"

float sBumpTile::SetVertexZ(TerraInterface* terra,int x,int y)
{
    float zi=terra->GetZf(x,y);
    if(zi<1)
        zi=-15;

    return zi;
}

sBumpTile::sBumpTile(cTileMap* tilemap, cTilemapTexturePool* pool, int lod, int xpos, int ypos)
{
    this->tilemap = tilemap;
    cTileMapRender* render = tilemap->GetTilemapRender();
    tile_pos.set(xpos,ypos);
    texPool = pool;
    texPage = texPool->allocPage();
    
    int numVertex = render->bumpNumVertices(lod);
    render->GetVertexPool()->CreatePage(vtx,VertexPoolParameter(numVertex, BUMP_VTXTYPE::fmt));

    Vect2f uvStart=texPool->getUVStart(texPage);
    uStart = uvStart.x;
    vStart = uvStart.y;
    vStep = texPool->getVStep() * static_cast<float>(1 << (bumpGeoScale[lod] - bumpTexScale[lod]));
    uStep = texPool->getUStep() * static_cast<float>(1 << (bumpGeoScale[lod] - bumpTexScale[lod]));

    init = false;
    LOD = lod;
    age = 0;

    for (int i=0; i<U_ALL; i++) {
        border_lod[i] = lod;
    }
}

sBumpTile::~sBumpTile()
{
    tilemap->GetTilemapRender()->GetVertexPool()->DeletePage(vtx);
    if (texPage >= 0)
        texPool->freePage(texPage);

    DeleteIndex();
}

void sBumpTile::DeleteIndex()
{
    for (auto& i : index) {
        if (i.index.page >= 0) {
            tilemap->GetTilemapRender()->GetIndexPool()->DeletePage(i.index);
        }
    }
    index.clear();
}

uint8_t* sBumpTile::LockTex(int& Pitch)
{
    return static_cast<uint8_t*>(texPool->lockPage(texPage, Pitch));
}

uint8_t *sBumpTile::LockVB()
{
    return static_cast<uint8_t*>(tilemap->GetTilemapRender()->GetVertexPool()->LockPage(vtx));
}

void sBumpTile::UnlockTex()
{
    texPool->unlockPage(texPage);
}

void sBumpTile::UnlockVB()
{
    tilemap->GetTilemapRender()->GetVertexPool()->UnlockPage(vtx);
}

inline int IUCLAMP(int val,int clamp)
{
    if(val<0)return 0;
    if(val>=clamp)return clamp-1;
    return val;
}

void sBumpTile::CalcTexture()
{
    int tilex = tilemap->GetTileSize().x;
    int tiley = tilemap->GetTileSize().y;
    int xStart = tile_pos.x * tilex;
    int yStart = tile_pos.y * tiley;
    int xFinish = xStart + tilex;
    int yFinish = yStart + tiley;
    int Pitch = 0;
    uint8_t* texRect = LockTex(Pitch);
    int dd = 1 << bumpTexScale[LOD];

    TerraInterface* terra = tilemap->GetTerra();
    terra->GetTileColor(
            texRect,
            Pitch,
            xStart - cTilemapTexturePool::TEXTURE_BORDER * dd,
            yStart - cTilemapTexturePool::TEXTURE_BORDER * dd,
            xFinish + cTilemapTexturePool::TEXTURE_BORDER * dd,
            yFinish + cTilemapTexturePool::TEXTURE_BORDER * dd,
            dd
    );

    UnlockTex();
}

bool sBumpTile::ReuseTextureFrom(sBumpTile& source)
{
    if (texPool != source.texPool || source.texPage < 0)
        return false;

    texPool->freePage(texPage);
    texPage = source.texPage;
    source.texPage = -1;
    Vect2f uvStart = texPool->getUVStart(texPage);
    uStart = uvStart.x;
    vStart = uvStart.y;
    initial_texture_reused = true;
    return true;
}

bool sBumpTile::TakeInitialTextureReuse()
{
    const bool reused = initial_texture_reused;
    initial_texture_reused = false;
    return reused;
}

void sBumpTile::Calc(bool update_texture, bool reuse_texture)
{
    if(update_texture && !reuse_texture) {
        CalcTexture();
    }
    CalcPoint(!update_texture, update_texture);
    init = true;
}

void sBumpTile::CalcPoint(bool reuse_point_players, bool invalidate_topology)
{
    Column** columns = tilemap->GetColumn();
    Vect2i pos=tile_pos;

    cTileMapRender* render = tilemap->GetTilemapRender();
    render->IncUpdate(this);

    int tilenumber = tilemap->GetZeroplastNumber();
    int step=bumpGeoScale[LOD];

    char dd=TILEMAP_SIZE>>step;
    int xstep=1<<step;
    int xstep2=1<<(step-1);

    int minx=pos.x*TILEMAP_SIZE;
    int miny=pos.y*TILEMAP_SIZE;
    int maxx=minx+TILEMAP_SIZE;
    int maxy=miny+TILEMAP_SIZE;

    int ddv=dd+1;
    VectDelta* points = render->GetDeltaBuffer();
    const size_t point_count = static_cast<size_t>(ddv) * ddv;
    const bool use_point_base_cache =
            reuse_point_players && point_base_cache.size() == point_count;

    if (use_point_base_cache) {
        std::copy(point_base_cache.begin(), point_base_cache.end(), points);
    } else {
        for (int y=0;y<ddv;y++) {
            for (int x = 0; x < ddv; x++) {
                const size_t point_index = static_cast<size_t>(x + y * ddv);
                VectDelta& p = points[point_index];
                p.set(x * xstep + minx, y * xstep + miny);
                p.delta.set(0, 0);
                p.delta2 = INT_MAX;
                p.player = 0;
                p.fix = 0;

                int xx = min(p.x, maxx - 1);
                int yy = min(p.y, maxy - 1);
                if (columns) {
                    for (int player = 0; player < tilenumber; player++) {
                        if (columns[player]->filled(xx, yy)) {
                            xassert(p.player == 0);
                            p.player = player + 1;
                        }
                    }
                }
            }
        }
        point_base_cache.assign(points, points + point_count);
    }

    int fix_out = FixLine(points,ddv);

    const bool reuse_interior_points =
            reuse_point_players && interior_point_valid &&
            interior_point_cache.size() == point_count;
    if (reuse_interior_points) {
        for (int y = 1; y < dd; ++y) {
            for (int x = 1; x < dd; ++x) {
                const size_t point_index = static_cast<size_t>(x + y * ddv);
                points[point_index] = interior_point_cache[point_index];
            }
        }
    }

    if (!reuse_point_players) {
        boundary_region_candidates.clear();
        boundary_region_candidates_valid = false;
    }

    auto projectBoundary = [&](int player, const Vect2s& source) {
        Vect2i p(source.x, source.y), pround;
        VectDelta* pnt = NULL;
        char dd2 = dd / 2;
        pround.x = (p.x - minx + xstep) >> (step + 1);
        pround.y = (p.y - miny + xstep) >> (step + 1);
        if (!(pround.x < 0 || pround.x > dd2 || pround.y < 0 || pround.y > dd2)) {
            pround.x *= 2; pround.y *= 2;
            pnt = &points[pround.x + pround.y * ddv];
            if (!pnt->fix) pnt = NULL;
        }
        if (!pnt) {
            pround.x = (p.x - minx + xstep2) >> step;
            pround.y = (p.y - miny + xstep2) >> step;
            if (pround.x < 0 || pround.x > dd || pround.y < 0 || pround.y > dd) return;
        }
        if (pround.x > 0 && pround.x < dd && pround.y > 0 && pround.y < dd) return;
        pnt = &points[pround.x + pround.y * ddv];
        if (pnt->fix & VectDelta::FIX_FIXED) {
            pround.x &= ~1; pround.y &= ~1;
            pnt = &points[pround.x + pround.y * ddv];
        }
        int delta2 = sqr(pnt->x - p.x) + sqr(pnt->y - p.y);
        if (delta2 >= pnt->delta2) return;
        pnt->delta2 = delta2; pnt->delta = p - *pnt; pnt->player = -1;
        if (pnt->fix & VectDelta::FIX_RIGHT) points[pround.x + 1 + pround.y * ddv].copy_no_fix(*pnt);
        if (pnt->fix & VectDelta::FIX_BOTTOM) points[pround.x + (pround.y + 1) * ddv].copy_no_fix(*pnt);
    };

    if (reuse_point_players && boundary_region_candidates_valid) {
        for (const auto& candidate : boundary_region_candidates)
            projectBoundary(candidate.player, candidate.point);
    } else {
    //int preceeded_point=0;
    for (int player=0;player<tilenumber;player++) {
        char dd2=dd/2;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                Vect2i cur_tile_pos(tile_pos.x + x, tile_pos.y + y);
                if (cur_tile_pos.x >= 0 && cur_tile_pos.x < tilemap->GetTileNumber().x)
                    if (cur_tile_pos.y >= 0 && cur_tile_pos.y < tilemap->GetTileNumber().y) {
                        std::vector<Vect2s>* region = tilemap->GetCurRegion(cur_tile_pos, player);
                        std::vector<Vect2s>::iterator it;
                        //preceeded_point += region->size();

                        /*
                        bool fix_border_x = false;
                        bool fix_border_y = false;
                        if (x == -1 && (fix_out & (1 << U_LEFT)))
                            fix_border_x = true;
                        if (y == -1 && (fix_out & (1 << U_TOP)))
                            fix_border_y = true;
                        if (x == +1 && (fix_out & (1 << U_RIGHT)))
                            fix_border_x = true;
                        if (y == +1 && (fix_out & (1 << U_BOTTOM)))
                            fix_border_y = true;
                        */

                        FOR_EACH(*region, it) {
                            Vect2i p(it->x, it->y);

                            if (!reuse_point_players) {
                                const int regular_x = (p.x - minx + xstep2) >> step;
                                const int regular_y = (p.y - miny + xstep2) >> step;
                                const int stitched_x =
                                        ((p.x - minx + xstep) >> (step + 1)) * 2;
                                const int stitched_y =
                                        ((p.y - miny + xstep) >> (step + 1)) * 2;
                                if (regular_x == 0 || regular_x == dd ||
                                        regular_y == 0 || regular_y == dd ||
                                        stitched_x == 0 || stitched_x == dd ||
                                        stitched_y == 0 || stitched_y == dd) {
                                    boundary_region_candidates.push_back({*it, player});
                                }
                            }

                            Vect2i pround;
                            VectDelta* pnt = NULL;

                            pround.x = (p.x - minx + xstep) >> (step + 1);
                            pround.y = (p.y - miny + xstep) >> (step + 1);
                            if (!(pround.x < 0 || pround.x > dd2 || pround.y < 0 || pround.y > dd2)) {
                                pround.x *= 2;
                                pround.y *= 2;
                                pnt = &points[pround.x + pround.y * ddv];
                                if (!pnt->fix) {
                                    pnt = NULL;
                                }
                            }

                            if (!pnt) {
                                pround.x = (p.x - minx + xstep2) >> step;
                                pround.y = (p.y - miny + xstep2) >> step;
                                if (pround.x < 0 || pround.x > dd || pround.y < 0 || pround.y > dd)
                                    continue;
                            }

                            if (pround.x < 0) {
                                VISASSERT(0);
                                pround.x = 0;
                            }
                            if (pround.y < 0) {
                                VISASSERT(0);
                                pround.y = 0;
                            }
                            if (pround.x > dd) {
                                VISASSERT(0);
                                pround.x = dd;
                            }
                            if (pround.y > dd) {
                                VISASSERT(0);
                                pround.y = dd;
                            }

                            // A pure LOD stitch cannot affect an interior
                            // point.  Its resolved projection was restored
                            // from the terrain-revision cache above.
                            if (reuse_interior_points && pround.x > 0 &&
                                    pround.x < dd && pround.y > 0 &&
                                    pround.y < dd)
                                continue;

                            pnt = &points[pround.x + pround.y * ddv];
                            if (pnt->fix & VectDelta::FIX_FIXED) {
                                pround.x &= ~1;
                                pround.y &= ~1;
                                pnt = &points[pround.x + pround.y * ddv];
                                VISASSERT(pnt->fix & (VectDelta::FIX_RIGHT | VectDelta::FIX_BOTTOM));
                            }

                            int delta2 = sqr(pnt->x - p.x) + sqr(pnt->y - p.y);
                            if (delta2 < pnt->delta2) {
                                pnt->delta2 = delta2;
                                pnt->delta = p - *pnt;
                                pnt->player = -1;
                                if (pnt->fix) {
                                    if (pnt->fix & VectDelta::FIX_RIGHT) {
                                        VectDelta& p = points[pround.x + 1 + pround.y * ddv];
                                        VISASSERT(pnt->x == p.x && pnt->y == p.y);
                                        VISASSERT(p.fix == VectDelta::FIX_FIXED);
                                        p.copy_no_fix(*pnt);
                                    }
                                    if (pnt->fix & VectDelta::FIX_BOTTOM) {
                                        VectDelta& p = points[pround.x + (pround.y + 1) * ddv];
                                        VISASSERT(pnt->x == p.x && pnt->y == p.y);
                                        VISASSERT(p.fix == VectDelta::FIX_FIXED);
                                        p.copy_no_fix(*pnt);
                                    }
                                }
                            }
                        }
                    }
            }
        }
    }
    }

    if (!reuse_interior_points) {
        interior_point_cache.assign(points, points + point_count);
        interior_point_valid = true;
    }
    if (!reuse_point_players)
        boundary_region_candidates_valid = true;

    std::vector<std::vector<sPolygon>>& index = render->GetIndexBuffer();
    const bool reuse_topology = !invalidate_topology && topology_valid;
    const bool reuse_interior_topology =
            !reuse_topology && !invalidate_topology && interior_topology_valid;
    if (reuse_topology) {
        index = topology_cache;
    } else if (reuse_interior_topology) {
        index = interior_topology_cache;
    } else {
        index.resize(tilenumber+1);
        for(int i=0;i<index.size();i++) {
            index[i].clear();
        }
        interior_topology_cache.resize(tilenumber+1);
        for (auto& player_index : interior_topology_cache) {
            player_index.clear();
        }
    }

    sPolygon poly;
    for (int y=0; !reuse_topology && y<dd; y++) {
        for(int x=0;x<dd;x++) {
            // LOD stitching changes only points on the outer grid lines, so
            // cells strictly inside this ring retain their prior topology.
            const bool interior_cell = x > 0 && x < dd - 1 &&
                    y > 0 && y < dd - 1;
            if (reuse_interior_topology && interior_cell)
                continue;
            int base=x+y*ddv;
            VectDelta *p00,*p01,*p10,*p11;
            p00=points+base;
            p01=points+(base+1);
            p10=points+(base+ddv);
            p11=points+(base+ddv+1);

            bool no;
            char cur_player_add;

#define X(p) \
		{ \
			if(cur_player_add<0) \
				cur_player_add=p->player; \
			else \
				if(p->player>=0 && cur_player_add!=p->player)\
					no=true;\
		}

            if(p01->player==p10->player || p01->player<0 || p10->player<0)
            {
                cur_player_add=-1;
                no=false;
                X(p00);
                X(p01);
                X(p10);

                if(cur_player_add<0 || no)
                {
                    int x=(p00->x+p00->delta.x+p01->x+p01->delta.x+p10->x+p10->delta.x)/3;
                    int y=(p00->y+p00->delta.y+p01->y+p01->delta.y+p10->y+p10->delta.y)/3;
                    cur_player_add=0;
                    if(columns)
                        for(int i=0;i<tilenumber;i++)
                            if(columns[i]->filled(x,y))
                            {
                                cur_player_add=i+1;
                                break;
                            }
                }

                xassert(cur_player_add>=0 && cur_player_add<=tilenumber);
                poly.set(base,base+ddv,base+1);
                index[cur_player_add].push_back(poly);
                if (!reuse_interior_topology && interior_cell)
                    interior_topology_cache[cur_player_add].push_back(poly);

                cur_player_add=-1;
                no=false;
                X(p01);
                X(p10);
                X(p11);

                if(cur_player_add<0 || no)
                {
                    int x=(p01->x+p01->delta.x+p10->x+p10->delta.x+p11->x+p11->delta.x)/3;
                    int y=(p01->y+p01->delta.y+p10->y+p10->delta.y+p11->y+p11->delta.y)/3;
                    cur_player_add=0;
                    if(columns)
                        for(int i=0;i<tilenumber;i++)
                            if(columns[i]->filled(x,y))
                            {
                                cur_player_add=i+1;
                                break;
                            }
                }

                xassert(cur_player_add>=0 && cur_player_add<=tilenumber);
                poly.set(base+ddv,base+ddv+1,base+1);
                index[cur_player_add].push_back(poly);
                if (!reuse_interior_topology && interior_cell)
                    interior_topology_cache[cur_player_add].push_back(poly);
            }else
            {
                cur_player_add=-1;
                no=false;
                X(p00);
                X(p01);
                X(p11);


                if(cur_player_add<0 || no)
                {
                    int x=(p00->x+p00->delta.x+p01->x+p01->delta.x+p11->x+p11->delta.x)/3;
                    int y=(p00->y+p00->delta.y+p01->y+p01->delta.y+p11->y+p11->delta.y)/3;
                    cur_player_add=0;
                    if(columns)
                        for(int i=0;i<tilenumber;i++)
                            if(columns[i]->filled(x,y))
                            {
                                cur_player_add=i+1;
                                break;
                            }
                }

                xassert(cur_player_add>=0 && cur_player_add<=tilenumber);
                poly.set(base,base+ddv+1,base+1);
                index[cur_player_add].push_back(poly);
                if (!reuse_interior_topology && interior_cell)
                    interior_topology_cache[cur_player_add].push_back(poly);

                cur_player_add=-1;
                no=false;
                X(p00);
                X(p10);
                X(p11);

                if(cur_player_add<0 || no)
                {
                    int x=(p00->x+p00->delta.x+p10->x+p10->delta.x+p11->x+p11->delta.x)/3;
                    int y=(p00->y+p00->delta.y+p10->y+p10->delta.y+p11->y+p11->delta.y)/3;
                    cur_player_add=0;
                    if(columns)
                        for(int i=0;i<tilenumber;i++)
                            if(columns[i]->filled(x,y))
                            {
                                cur_player_add=i+1;
                                break;
                            }
                }

                xassert(cur_player_add>=0 && cur_player_add<=tilenumber);
                poly.set(base,base+ddv,base+ddv+1);
                index[cur_player_add].push_back(poly);
                if (!reuse_interior_topology && interior_cell)
                    interior_topology_cache[cur_player_add].push_back(poly);
            }
        }
    }
#undef X
    if (!reuse_interior_topology)
        interior_topology_valid = true;
    if (!reuse_topology) {
        topology_cache = index;
        topology_valid = true;
    }

    /////////////////////set vertex buffer
    int is = tilemap->GetTileSize().x; // tile size in vMap units
    int js = tilemap->GetTileSize().y;
    int xMap = is * tilemap->GetTileNumber().x; // vMap size
    int yMap = js * tilemap->GetTileNumber().y;
    int xStart = tile_pos.x * is;
    int yStart = tile_pos.y * js;
    int xFinish = xStart + is;
    int yFinish = yStart + js;

    float ux_step=uStep/static_cast<float>(1 << bumpGeoScale[LOD]);
    float ux_base=uStart-xStart*ux_step;
    float vy_step=vStep/static_cast<float>(1 << bumpGeoScale[LOD]);
    float vy_base=vStart-yStart*vy_step;


    BUMP_VTXTYPE* vb = reinterpret_cast<BUMP_VTXTYPE*>(LockVB());

    TerraInterface* terra = tilemap->GetTerra();
    const bool update_only_border_vertices = reuse_interior_points;

    for (int y = 0; y < ddv; y++) {
        for (int x = 0; x < ddv; x++) {
            if (update_only_border_vertices && x > 0 && x < dd &&
                    y > 0 && y < dd) {
                vb++;
                continue;
            }
            int i = x + y * ddv;
            VectDelta& p = points[i];
            int xx = p.x + p.delta.x;
            int yy = p.y + p.delta.y;

            vb->x = xx;
            vb->y = yy;
            vb->z = SetVertexZ(terra, IUCLAMP(xx, xMap), IUCLAMP(yy, yMap)); //terra->GetHZeroPlast();

            vb->u1() = xx * ux_step + ux_base;
            vb->v1() = yy * vy_step + vy_base;
            vb++;
        }
    }

    UnlockVB();

    ////////////////////set index buffer
    DeleteIndex();

    int num_non_empty=0;
    int one_player=0;
    for(int i=0;i<index.size();i++)
    {
        if(!index[i].empty())
        {
            num_non_empty++;
            one_player=i-1;
        }
    }

    if(num_non_empty<=1) {
        sBumpTile::index.resize(1);
        sBumpTile::index[0].player=one_player;
        sBumpTile::index[0].nindex=-1;
    } else {
#ifdef PERIMETER_DEBUG
        int sum_index=0;
#endif
        sBumpTile::index.resize(num_non_empty);
        int cur=0;
        for (int i = 0; i < index.size(); i++) {
            if (index[i].size() > 0) {
                int num = index[i].size() * 3;
#ifdef PERIMETER_DEBUG
                sum_index += num;
#endif
                int num2 = Power2up(num);

                IndexPoolManager* pool = render->GetIndexPool();
                pool->CreatePage(sBumpTile::index[cur].index, num2);
                indices_t* p = pool->LockPage(sBumpTile::index[cur].index);
                memcpy(p, index[i].data(), num * sizeof(indices_t));
                pool->UnlockPage(sBumpTile::index[cur].index);

                sBumpTile::index[cur].nindex = num;
                sBumpTile::index[cur].player = i - 1;
                cur++;
            }
        }

#ifdef PERIMETER_DEBUG
        VISASSERT(render->bumpNumIndex(LOD)==sum_index);
#endif
    }
}

int sBumpTile::FixLine(VectDelta* points, int ddv)
{
    int fix_out=0;
    //int dmax=ddv*ddv;
    int dv=ddv-1;
    for(int side=0;side<U_ALL;side++)
    {
        if(border_lod[side]<=LOD)
            continue;
//		VISASSERT(border_lod[side]==LOD+1);

        fix_out|=1<<side;

        VectDelta* cur;
        int delta;
        char fix=0;
        switch(side)
        {
            case U_LEFT:
                cur=points;
                delta=ddv;
                fix=VectDelta::FIX_BOTTOM;
                break;
            case U_RIGHT:
                cur=points+ddv-1;
                delta=ddv;
                fix=VectDelta::FIX_BOTTOM;
                points[ddv*ddv-1].fix|=VectDelta::FIX_FIXEMPTY;
                break;
            case U_TOP:
                cur=points;
                delta=1;
                fix=VectDelta::FIX_RIGHT;
                break;
            case U_BOTTOM:
                cur=points+ddv*(ddv-1);
                delta=1;
                fix=VectDelta::FIX_RIGHT;
                points[ddv*ddv-1].fix|=VectDelta::FIX_FIXEMPTY;
                break;
        }

        for(int i=0;i<dv;i+=2,cur+=2*delta)
        {
//			VISASSERT(cur-points<dmax);
            VectDelta* p=cur+delta;
//			VISASSERT(p-points<dmax);
            p->copy_no_fix(*cur);
            //*p=*cur;
            VISASSERT(p->fix==0);
            cur->fix|=fix;
            p->fix|=VectDelta::FIX_FIXED;
        }
    }


    return fix_out;
}

void sBumpTile::ToList() {
    texPool->tileRenderList.push_back(this);
}

