﻿/**
@brief    OBJ用 ツール
@file     OBJDataTool.cpp
@author   Fumi.Iseki (C)
*/

#include  "OBJDataTool.h"


using namespace jbxl;


///////////////////////////////////////////////////////////////////////////////////////////////////
//

OBJData::~OBJData(void)
{
    this->free();
}


void  OBJData::init(int n)
{
    this->obj_name    = init_Buffer();
    this->num_obj     = n;
    this->phantom_out = true;
    this->no_offset   = false;

    this->forUnity    = true;
    this->forUE       = false;

    this->engine      = JBXL_3D_ENGINE_UE;
    this->next        = NULL;
    this->geo_node    = NULL;
    this->mtl_node    = NULL;
    this->affineTrans = NULL;
}


void  OBJData::free(void)
{
    free_Buffer(&(this->obj_name));

    this->delAffineTrans();
    this->affineTrans = NULL;

    delete(this->geo_node);
    delete(this->mtl_node);
    this->geo_node = NULL;
    this->mtl_node = NULL;

    this->delete_next();
}


void  OBJData::setEngine(int e)
{
    this->setUnity(false);
    this->setUE(false);
    //
    this->engine = e;
    if (e == JBXL_3D_ENGINE_UNITY)   this->setUnity(true);
    else if (e == JBXL_3D_ENGINE_UE) this->setUE(true);

    return;
}


void  OBJData::delete_next(void)
{
    if (this->next==NULL) return;

    OBJData* _next = this->next;
    while (_next!=NULL) {
        OBJData* _curr_node = _next;
        _next = _next->next;
        _curr_node->next = NULL;
        delete(_curr_node);
    }
    this->next = NULL;
}


void  OBJData::addObject(MeshObjectData* meshdata, bool collider)
{
    if (meshdata==NULL) return;

    OBJData* ptr_obj = this;
    while (ptr_obj->next!=NULL) ptr_obj = ptr_obj->next;
    //
    ptr_obj->next = new OBJData(-1);
    this->num_obj++;

    if (meshdata->affineTrans!=NULL) { // Grass の場合は NULL
        ptr_obj->next->setAffineTrans(*meshdata->affineTrans);
    }
    ptr_obj->next->obj_name = dup_Buffer(meshdata->data_name);

    MeshFacetNode* facet = meshdata->facet;
    OBJFacetGeoNode** _geo_node = &(ptr_obj->next->geo_node);
    OBJFacetMtlNode** _mtl_node = &(ptr_obj->next->mtl_node);
    while (facet!=NULL) {
        if (facet->num_vertex != facet->num_texcrd) {
            PRINT_MESG("OBJData::addObject: Error: missmatch vertex and uvmap number! (%d != %d)\n", facet->num_vertex, facet->num_texcrd);
            facet = facet->next;
            continue;
        }

        // UV Map and PLANAR Texture
        if (facet->material_param.mapping == MATERIAL_MAPPING_PLANAR) {
            Vector<double> scale(1.0, 1.0, 1.0);
            if (meshdata->affineTrans!=NULL) scale = meshdata->affineTrans->scale;
            facet->generatePlanarUVMap(scale, facet->texcrd_value);
        }
        facet->execAffineTransUVMap(facet->texcrd_value, facet->num_vertex);

        // Geometory
        *_geo_node = new OBJFacetGeoNode();
        *_mtl_node = new OBJFacetMtlNode();
        (*_geo_node)->num_index = facet->num_index;
        (*_geo_node)->data_index = (int*)malloc(sizeof(int)*(*_geo_node)->num_index);
        if ((*_geo_node)->data_index != NULL) {
            for (int i = 0; i < (*_geo_node)->num_index; i++) {
                (*_geo_node)->data_index[i] = facet->data_index[i];
            }
        }
        else {
            return;
        }

        (*_geo_node)->collider = collider;
        (*_geo_node)->num_vertex = facet->num_vertex;
        (*_geo_node)->vv = (Vector<double>*)malloc(sizeof(Vector<double>)*(*_geo_node)->num_vertex);
        (*_geo_node)->vn = (Vector<double>*)malloc(sizeof(Vector<double>)*(*_geo_node)->num_vertex);
        (*_geo_node)->vt = (UVMap<double>*) malloc(sizeof(UVMap<double>) *(*_geo_node)->num_vertex);
        if ((*_geo_node)->vv != NULL && (*_geo_node)->vn != NULL && (*_geo_node)->vt != NULL) {
            for (int i = 0; i < (*_geo_node)->num_vertex; i++) {
                (*_geo_node)->vv[i] = facet->vertex_value[i];
                (*_geo_node)->vn[i] = facet->normal_value[i];
                (*_geo_node)->vt[i] = facet->texcrd_value[i];
            }
        }
        else {
            freeNull((*_geo_node)->vv);
            freeNull((*_geo_node)->vn);
            freeNull((*_geo_node)->vt);
            freeNull((*_geo_node)->data_index);
            return;
        }

        // Material
        (*_geo_node)->material = dup_Buffer(facet->material_id);
        (*_mtl_node)->material = dup_Buffer(facet->material_id);
        (*_mtl_node)->same_material = facet->same_material;
        if (!(*_mtl_node)->same_material) {
            (*_mtl_node)->material_param.dup(facet->material_param);
            (*_mtl_node)->setup_params();
        }

        _geo_node = &((*_geo_node)->next);
        _mtl_node = &((*_mtl_node)->next);
        facet = facet->next;
    }
}


/**
Vector<double>  OBJData::execAffineTrans(void)

OBJデータの Affine変換を行う．
no_offset が trueの場合，データの中心を原点に戻し，実際の位置をオフセットで返す．

@retval データのオフセット．
*/
Vector<double>  OBJData::execAffineTrans(void)
{
    Vector<double> center(0.0, 0.0, 0.0);

    OBJData* obj = this->next;  // Top はアンカー
    if (obj!=NULL && no_offset) center = obj->affineTrans->shift;

    while (obj!=NULL) {
        if (obj->affineTrans!=NULL) {
            OBJFacetGeoNode* facet = obj->geo_node;
            while(facet!=NULL) {
                for (int i=0; i<facet->num_vertex; i++) {
                    facet->vv[i] = obj->affineTrans->execTrans(facet->vv[i]) - center;  // 頂点座標
                    facet->vn[i] = obj->affineTrans->execRotate(facet->vn[i]);          // 法線ベクトル
                }
                facet = facet->next;
            }
        }
        obj = obj->next;
    }

    return center;
}


void  OBJData::outputFile(const char* fname, const char* out_path, const char* tex_dirn, const char* mtl_dirn)
{
    char* packname = pack_head_tail_char(get_file_name(fname), ' ');
    Buffer file_name = make_Buffer_bystr(packname);
    ::free(packname);

    canonical_filename_Buffer(&file_name);
    if (file_name.buf[0]=='.') file_name.buf[0] = '_';
    //
    Buffer obj_path;
#ifdef WIN32
    if (out_path==NULL) obj_path = make_Buffer_bystr(".\\");
#else
    if (out_path==NULL) obj_path = make_Buffer_bystr("./");
#endif
    else                obj_path = make_Buffer_bystr(out_path);
    //
    Buffer rel_tex;    //  相対パス
    if (tex_dirn==NULL) rel_tex = make_Buffer_bystr("");
    else                rel_tex = make_Buffer_bystr(tex_dirn);
    Buffer rel_mtl;    //  相対パス
    if (mtl_dirn==NULL) rel_mtl = make_Buffer_bystr("");
    else                rel_mtl = make_Buffer_bystr(mtl_dirn);
    //
    cat_Buffer(&file_name, &rel_mtl);
    change_file_extension_Buffer(&rel_mtl, ".mtl");

    Buffer mtl_path = dup_Buffer(obj_path);
    cat_Buffer(&rel_mtl, &mtl_path);

    cat_Buffer(&file_name, &obj_path);
    change_file_extension_Buffer(&obj_path, ".obj");

    this->output_mtl((char*)mtl_path.buf, (char*)rel_tex.buf);  // mtl file
    this->output_obj((char*)obj_path.buf, (char*)rel_mtl.buf);  // obj file
    //
    free_Buffer(&file_name);
    free_Buffer(&obj_path);
    free_Buffer(&mtl_path);
    free_Buffer(&rel_mtl);
    free_Buffer(&rel_tex);
    //
    return;
}


void  OBJData::output_mtl(const char* mtl_path, const char* tex_dirn)
{
    FILE* fp = fopen(mtl_path, "wb");
    if (fp==NULL) return;

    fprintf(fp, "# %s\n", OBJDATATOOL_STR_MTLFL);
    fprintf(fp, "# %s\n", OBJDATATOOL_STR_TOOL);
    fprintf(fp, "# %s\n", OBJDATATOOL_STR_AUTHOR);
    fprintf(fp, "# %s\n", OBJDATATOOL_STR_VER);

    tList* material_list = NULL;
    OBJData* obj = this->next;
    while (obj!=NULL) {
        OBJFacetMtlNode* node = obj->mtl_node;
        while(node!=NULL) {
            if (!node->same_material) {
                // check material
                tList* mat = search_key_tList(material_list, (char*)node->material.buf, 1);
                if (mat==NULL) {
                    tList* lt = add_tList_node_str(material_list, node->material.buf, NULL);
                    if (material_list==NULL) material_list = lt;

                    // outpust
                    fprintf(fp, "#\n");
                    fprintf(fp, "newmtl %s\n", node->material.buf+1);                   // マテリアル名

                    if (node->map_kd.buf!=NULL) {
                        fprintf(fp, "map_Kd %s%s\n", tex_dirn, node->map_kd.buf);       // Texture ファイル名
                    }
                    if (node->map_ks.buf!=NULL) {
                        fprintf(fp, "map_Ks %s%s\n", tex_dirn, node->map_ks.buf);       // Specular Map ファイル名
                    }
                    if (node->map_bump.buf!=NULL) {
                        fprintf(fp, "map_bump %s%s\n", tex_dirn, node->map_bump.buf);   // Bump Map ファイル名
                    }

                    fprintf(fp, "Ka %f %f %f\n", (float)node->ka.x, (float)node->ka.y, (float)node->ka.z);
                    fprintf(fp, "Kd %f %f %f\n", (float)node->kd.x, (float)node->kd.y, (float)node->kd.z);
                    fprintf(fp, "Ks %f %f %f\n", (float)node->ks.x, (float)node->ks.y, (float)node->ks.z);

                    fprintf(fp, "d %f\n",  (float)node->dd);
                    fprintf(fp, "Ni %f\n", (float)node->ni);

                    fprintf(fp, "illum %d\n", node->illum);
                }
            }
            node = node->next;
        }
        obj = obj->next;
    }
    del_tList(&material_list);

    fclose(fp);
    return;
}


void  OBJData::output_obj(const char* obj_path, const char* mtl_path)
{
    FILE* fp = fopen(obj_path, "wb");
    if (fp==NULL) return;

    fprintf(fp, "# %s\n", OBJDATATOOL_STR_OBJFL);
    fprintf(fp, "# %s\n", OBJDATATOOL_STR_TOOL);
    fprintf(fp, "# %s\n", OBJDATATOOL_STR_AUTHOR);
    fprintf(fp, "# %s\n", OBJDATATOOL_STR_VER);

    int facet_num = 0;
    int file_num  = 1;

    int p_num = 1;
    OBJData* obj = this->next;
    while (obj!=NULL) {
        // file division for Unity
        if (facet_num>OBJDATATOOL_MAX_FACET && this->engine==JBXL_3D_ENGINE_UNITY) {
            fclose(fp);
            Buffer obj_file = make_Buffer_str(obj_path);
            del_file_extension_Buffer(&obj_file);
            cat_s2Buffer("_", &obj_file);
            cat_s2Buffer(itostr(file_num), &obj_file);
            cat_s2Buffer(".obj", &obj_file);

            fp = fopen((char*)obj_file.buf, "wb");
            free_Buffer(&obj_file);
            if (fp==NULL) return;

            file_num++;
            facet_num = 0;
            p_num = 1;
        }

        fprintf(fp, "# \n# SHELL\n");
        OBJFacetGeoNode* facet = obj->geo_node;
        while(facet!=NULL) {
            fprintf(fp, "#\n# FACET\n");
            fprintf(fp, "mtllib %s\n", mtl_path);               // ファイル名

            for (int i=0; i<facet->num_vertex; i++) {
                Vector<float> vv = Vector<float>((float)facet->vv[i].x, (float)facet->vv[i].y, (float)facet->vv[i].z);
                if (this->engine == JBXL_3D_ENGINE_UE) {
                    fprintf(fp, "v %f %f %f\n", vv.x*100.f, vv.y*100.f, vv.z*100.f);    // for UE
                }
                else {
                    fprintf(fp, "v %f %f %f\n", vv.x, vv.z, -vv.y);                     // for Unity
                }
            }
            for (int i=0; i<facet->num_vertex; i++) {
                UVMap<float> vt = UVMap<float>((float)facet->vt[i].u, (float)facet->vt[i].v);
                fprintf(fp, "vt %f %f\n", vt.u, vt.v);
            }
            for (int i=0; i<facet->num_vertex; i++) {
                Vector<float> vn = Vector<float>((float)facet->vn[i].x, (float)facet->vn[i].y, (float)facet->vn[i].z);
                if (this->engine == JBXL_3D_ENGINE_UE) {
                    fprintf(fp, "vn %f %f %f\n", vn.x, vn.y, vn.z);                     // for UE
                }
                else {
                    fprintf(fp, "vn %f %f %f\n", vn.x, vn.z, -vn.y);                    // for Unity
                }
            }
            //
            fprintf(fp, "usemtl %s\n", facet->material.buf+1);  // マテリアル名
            for (int i=0; i<facet->num_index/3; i++) {
                fprintf(fp, "f %d/%d/%d", facet->data_index[i*3  ]+p_num, facet->data_index[i*3  ]+p_num, facet->data_index[i*3  ]+p_num);
                fprintf(fp, " %d/%d/%d ", facet->data_index[i*3+1]+p_num, facet->data_index[i*3+1]+p_num, facet->data_index[i*3+1]+p_num);
                fprintf(fp, "%d/%d/%d\n", facet->data_index[i*3+2]+p_num, facet->data_index[i*3+2]+p_num, facet->data_index[i*3+2]+p_num);
            }
            p_num += facet->num_vertex;
            facet_num++;
            //
            facet = facet->next;
        }
        obj = obj->next;
    }

    fclose(fp);
    return;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
//
OBJFacetGeoNode::~OBJFacetGeoNode(void)
{
    this->free();
}


void  OBJFacetGeoNode::init(void)
{
    this->material = init_Buffer();
    this->collider = true;
    this->num_index  = 0;
    this->num_vertex = 0;

    this->data_index = NULL;
    this->vv = this->vn = NULL;
    this->vt = NULL;
    this->uvmap_trans = NULL;
    this->next = NULL;
}


void OBJFacetGeoNode::free(void)
{
    free_Buffer(&(this->material));
    this->num_index  = 0;
    this->num_vertex = 0;

    if (this->data_index!=NULL) ::free(this->data_index);
    this->data_index = NULL;

    if (this->vv!=NULL) ::free(this->vv);
    if (this->vn!=NULL) ::free(this->vn);
    if (this->vt!=NULL) ::free(this->vt);
    this->vv = this->vn = NULL;
    this->vt = NULL;

    freeAffineTrans(this->uvmap_trans);
    this->uvmap_trans = NULL;

    this->delete_next();
}


void  OBJFacetGeoNode::delete_next(void)
{
    if (next==NULL) return;

    OBJFacetGeoNode* _next = this->next;
    while (_next!=NULL) {
        OBJFacetGeoNode* _curr_node = _next;
        _next = _next->next;
        _curr_node->next = NULL;
        delete(_curr_node);
    }
    this->next = NULL;
}



///////////////////////////////////////////////////////////////////////////////////////////////////
//
//
OBJFacetMtlNode::~OBJFacetMtlNode(void)
{
    this->free();
}


void  OBJFacetMtlNode::init(void)
{
    this->same_material  = false;

    this->material = init_Buffer();
    this->map_kd   = init_Buffer();
    this->map_ks   = init_Buffer();
    this->map_bump = init_Buffer();
    memset(&(this->material_param), 0, sizeof(this->material_param));
    
    this->ka = Vector<double>(1.0, 1.0, 1.0);
    this->kd = Vector<double>(1.0, 1.0, 1.0);
    this->ks = Vector<double>(1.0, 1.0, 1.0);

    this->dd = 1.0;
    this->ni = 1.0;
    this->illum = 2;

    this->next = NULL;
}


void  OBJFacetMtlNode::free(void)
{
    free_Buffer(&(this->material));
    free_Buffer(&(this->map_kd));
    free_Buffer(&(this->map_ks));
    free_Buffer(&(this->map_bump));

    this->material_param.free();
    this->delete_next();
}


void  OBJFacetMtlNode::delete_next(void)
{
    if (this->next==NULL) return;

    OBJFacetMtlNode* _next = this->next;
    while (_next!=NULL) {
        OBJFacetMtlNode* _curr_node = _next;
        _next = _next->next;
        _curr_node->next = NULL;
        delete(_curr_node);
    }
    this->next = NULL;
}


void  OBJFacetMtlNode::setup_params(void)
{
    TextureParam texture = this->material_param.texture;
    TextureParam specmap = this->material_param.specmap;
    TextureParam bumpmap = this->material_param.bumpmap;

    if (texture.isSetTexture()) {       // map_Kd
        this->map_kd = make_Buffer_str(texture.getName());
        canonical_filename_Buffer(&this->map_kd);
    }
    if (specmap.isSetTexture()) {       // map_Ks
        this->map_ks = make_Buffer_str(specmap.getName());
        canonical_filename_Buffer(&this->map_ks);
    }
    if (bumpmap.isSetTexture()) {       // map_bump
        this->map_bump = make_Buffer_str(bumpmap.getName());
        canonical_filename_Buffer(&this->map_bump);
    }

    this->ka = Vector<double>(1.0, 1.0, 1.0);
    this->kd = texture.getColor();
    this->ks = specmap.getColor();

    //this->dd = this->material_param.getTransparent();
    this->dd = texture.getColor(3);
    this->ni = this->material_param.getShininess()*10.;
    if (this->ni<1.0) this->ni = 1.0;

    this->illum = 2;
/*
 0. Color on and Ambient off
 1. Color on and Ambient on
 2. Highlight on
 3. Reflection on and Ray trace on
 4. Transparency: Glass on, Reflection: Ray trace on
 5. Reflection: Fresnel on and Ray trace on
 6. Transparency: Refraction on, Reflection: Fresnel off and Ray trace on
 7. Transparency: Refraction on, Reflection: Fresnel on and Ray trace on
 8. Reflection on and Ray trace off
 9. Transparency: Glass on, Reflection: Ray trace off
10. Casts shadows onto invisible surfaces
*/
    return;
}

