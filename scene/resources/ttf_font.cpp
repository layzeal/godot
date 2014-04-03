/*************************************************************************/
/*  ttf_font.cpp                                                         */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                    http://www.godotengine.org                         */
/*************************************************************************/
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                 */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/
#include "ttf_font.h"
#include "font.h"
#include "core/globals.h"
#include "core/os/file_access.h"
#include "core/io/image_loader.h"

#ifdef FREETYPE_ENABLED

#define ALTAS_SIZE 512

int TtfFont::instance_count=0;
FT_Library TtfFont::library=NULL;

bool TtfFont::load_ttf(const String& p_path) {
    data = FileAccess::get_file_as_array(p_path);
    ERR_FAIL_COND_V(data.size()==0,false);

	ERR_EXPLAIN("Error initializing FreeType.");
    ERR_FAIL_COND_V( library==NULL, false );

	print_line("loadfrom: "+p_path);
    FT_Error error = FT_New_Memory_Face( library, data.ptr(), data.size(), 0,&face );
	if ( error == FT_Err_Unknown_File_Format ) {
		ERR_EXPLAIN("Unknown font format.");
	} else if ( error ) {
		ERR_EXPLAIN("Error loading font.");
	}
	ERR_FAIL_COND_V(error,false);
    path = p_path;
    return true;
}

bool TtfFont::calc_size(int p_size, int& height, int& ascent, int& max_up, int& max_down) {

	FT_Error error = FT_Set_Char_Size(face,0,64*p_size,512,512);
	if ( error ) {
		ERR_EXPLAIN("Invalid font size. ");
		ERR_FAIL_COND_V( error,false );
	}
	error = FT_Set_Pixel_Sizes(face,0,p_size);

	FT_GlyphSlot slot = face->glyph;

	/* PRINT CHARACTERS TO INDIVIDUAL BITMAPS */
	FT_ULong  charcode;
	FT_UInt   gindex;

	max_up=-1324345; ///gibberish
	max_down=124232;

    charcode = FT_Get_First_Char( face, &gindex );

	int xsize=0;
	while ( gindex != 0 )
	{
		bool skip=false;
		error = FT_Load_Char( face, charcode, FT_LOAD_RENDER );
		if (error) skip=true;
		else error = FT_Render_Glyph( face->glyph, ft_render_mode_normal );
		if (error) {
			skip=true;
		} else if (!skip) {
            skip=charcode>127;
		}

		if (charcode<=32) //??
			skip=true;

		if (skip) {
			if (charcode<=127)
				charcode=FT_Get_Next_Char(face,charcode,&gindex);
			else
				// break loop if charcode out of range(127)
				gindex=0;
			continue;
		}

		if  (charcode=='x')
			xsize=slot->bitmap.width;

		if (charcode<127) {
			if (slot->bitmap_top>max_up) {
				max_up=slot->bitmap_top;
			}

			if ( (slot->bitmap_top - slot->bitmap.rows)<max_down ) {
				max_down=slot->bitmap_top - slot->bitmap.rows;
			}
		}
		charcode=FT_Get_Next_Char(face,charcode,&gindex);
	}
	height=max_up-max_down;
	ascent=max_up;
    return true;
}

struct FontData {

	Vector<uint8_t> bitmap;
	int width,height;
	int ofs_x; //ofset to center, from ABOVE
	int ofs_y; //ofset to begining, from LEFT
	int valign; //vertical alignment
	int halign;
	float advance;
	int character;
	int glyph;

	int texture;
	Image blit;
	Point2i blit_ofs;
};

enum ColorType {
	COLOR_WHITE,
	COLOR_CUSTOM,
	COLOR_GRADIENT_RANGE,
	COLOR_GRADIENT_IMAGE
};

bool TtfFont::get_char(CharType p_char, Vector<Image *>& p_images, int& p_atlas_x, int& p_atlas_y, int& p_atlas_height, void *p_ch, const Dictionary& p_options) {
    Font::Character *ch = (Font::Character *) p_ch;

    int size=p_options["font/size"];

    int height=p_options["meta/height"];
    //int ascent=p_options["meta/ascent"];
    int max_up=p_options["meta/max_up"];
    int max_down=p_options["meta/max_down"];

	int font_spacing=0;

	FT_Error error = FT_Set_Char_Size(face,0,64*size,512,512);
	if ( error ) {
		ERR_EXPLAIN("Invalid font size. ");
		ERR_FAIL_COND_V( error,false );
	}
	error = FT_Set_Pixel_Sizes(face,0,size);

	FT_GlyphSlot slot = face->glyph;

	FT_ULong  charcode;
	FT_UInt   gindex;

	bool round_advance = p_options["advanced/round_advance"];

    FontData efd;

	bool skip=false;
	error = FT_Load_Char( face, p_char, FT_LOAD_RENDER );
	if (error) skip=true;
	else error = FT_Render_Glyph( face->glyph, ft_render_mode_normal );
	if (error)
		skip=true;
    if (p_char<=32) {
        skip=true;
    }

    if (skip) {
        if (p_char==' ') {
            efd.advance=0;
            efd.character=' ';
            efd.halign=0;
            efd.valign=0;
            efd.width=0;
            efd.height=0;
            efd.ofs_x=0;
            efd.ofs_y=0;
        	if (!FT_Load_Char( face, ' ', FT_LOAD_RENDER ) && !FT_Render_Glyph( face->glyph, ft_render_mode_normal )) {

        		efd.advance = slot->advance.x>>6; //round to nearest or store as float
        		efd.advance+=font_spacing;
        	} else {
                // use 'x' size
            	FT_Load_Char( face, 'x', FT_LOAD_RENDER );
                FT_Render_Glyph( face->glyph, ft_render_mode_normal );
                efd.advance=face->glyph->bitmap.width;
        		efd.advance+=font_spacing;
        	}
        }
        else
            return false;
    }
    else {
		efd.bitmap.resize( slot->bitmap.width*slot->bitmap.rows );
		efd.width=slot->bitmap.width;
		efd.height=slot->bitmap.rows;
		efd.character=p_char;
		efd.glyph=FT_Get_Char_Index(face,p_char);
        efd.valign=slot->bitmap_top;
        efd.halign=slot->bitmap_left;

		if (round_advance)
			efd.advance=(slot->advance.x+(1<<5))>>6;
		else
			efd.advance=slot->advance.x/float(1<<6);

		efd.advance+=font_spacing;

		for (int i=0;i<slot->bitmap.width;i++) {
			for (int j=0;j<slot->bitmap.rows;j++) {

				efd.bitmap[j*slot->bitmap.width+i]=slot->bitmap.buffer[j*slot->bitmap.width+i];
			}
		}
    }
	/* ADJUST THE VALIGN FOR CHARACTER */
    efd.valign=max_up-efd.valign;

	Color *color=memnew_arr(Color,height);
	int gradient_type=p_options["color/mode"];
	switch(gradient_type) {
		case COLOR_WHITE: {

			for(int i=0;i<height;i++){
				color[i]=Color(1,1,1,1);
			}

		} break;
		case COLOR_CUSTOM: {

			Color cc = p_options["color/color"];
			for(int i=0;i<height;i++){
				color[i]=cc;
			}

		} break;
		case COLOR_GRADIENT_RANGE: {

			Color src=p_options["color/begin"];
			Color to=p_options["color/end"];
			for(int i=0;i<height;i++){
				color[i]=src.linear_interpolate(to,i/float(height));
			}

		} break;
		case COLOR_GRADIENT_IMAGE: {
            String fp=p_options["color/image"];
	        if (fp.is_rel_path()) {
		        fp=Globals::get_singleton()->get_resource_path().plus_file(fp).simplify_path();
	        }

			Image img;
			Error err = ImageLoader::load_image(fp,&img);
			if (err==OK) {

				for(int i=0;i<height;i++){
					color[i]=img.get_pixel(0,i*img.get_height()/height);
				}
			} else {

				for(int i=0;i<height;i++){
					color[i]=Color(1,1,1,1);
				}
			}

		} break;
	}

    // render shadows
    if(efd.bitmap.size()>0)
    {
		int margin[4]={0,0,0,0};

		if (p_options["shadow/enabled"].operator bool()) {
			int r=p_options["shadow/radius"];
			Point2i ofs=Point2(p_options["shadow/offset"]);
			margin[ MARGIN_LEFT ] = MAX( r - ofs.x, 0);
			margin[ MARGIN_RIGHT ] = MAX( r + ofs.x, 0);
			margin[ MARGIN_TOP ] = MAX( r - ofs.y, 0);
			margin[ MARGIN_BOTTOM ] = MAX( r + ofs.y, 0);

		}

		if (p_options["shadow2/enabled"].operator bool()) {
			int r=p_options["shadow2/radius"];
			Point2i ofs=Point2(p_options["shadow2/offset"]);
			margin[ MARGIN_LEFT ] = MAX( r - ofs.x, margin[ MARGIN_LEFT ]);
			margin[ MARGIN_RIGHT ] = MAX( r + ofs.x, margin[ MARGIN_RIGHT ]);
			margin[ MARGIN_TOP ] = MAX( r - ofs.y, margin[ MARGIN_TOP ]);
			margin[ MARGIN_BOTTOM ] = MAX( r + ofs.y, margin[ MARGIN_BOTTOM ]);

		}

		Size2i s;
        s.width=efd.width+margin[MARGIN_LEFT]+margin[MARGIN_RIGHT];
		s.height=efd.height+margin[MARGIN_TOP]+margin[MARGIN_BOTTOM];
		Point2i o;
		o.x=margin[MARGIN_LEFT];
		o.y=margin[MARGIN_TOP];

        int ow=efd.width;
        int oh=efd.height;

		DVector<uint8_t> pixels;
		pixels.resize(s.x*s.y*4);

		DVector<uint8_t>::Write w = pixels.write();
		for(int y=0;y<s.height;y++) {

			int yc=CLAMP(y-o.y+efd.valign,0,height-1);
			Color c=color[yc];
			c.a=0;

			for(int x=0;x<s.width;x++) {

				int ofs=y*s.x+x;
				w[ofs*4+0]=c.r*255.0;
				w[ofs*4+1]=c.g*255.0;
				w[ofs*4+2]=c.b*255.0;
				w[ofs*4+3]=c.a*255.0;
			}
		}


		for(int si=0;si<2;si++) {

#define S_VAR(m_v) (String(si==0?"shadow/":"shadow2/")+m_v)
			if (p_options[S_VAR("enabled")].operator bool()) {
				int r = p_options[S_VAR("radius")];

				Color sc = p_options[S_VAR("color")];
				Point2i so=Point2(p_options[S_VAR("offset")]);

				float tr = p_options[S_VAR("transition")];

				Vector<uint8_t> s2buf;
				s2buf.resize(s.x*s.y);
				uint8_t *wa=s2buf.ptr();

				for(int j=0;j<s.x*s.y;j++){

					wa[j]=0;
				}

				// blit shadowa
				for(int x=0;x<ow;x++) {
					for(int y=0;y<oh;y++) {
						int ofs = (o.y+y+so.y)*s.x+x+o.x+so.x;
						wa[ofs]=efd.bitmap[y*ow+x];
					}
				}
				//blur shadow2 with separatable convolution

				if (r>0) {

					Vector<uint8_t> pixels2;
					pixels2.resize(s2buf.size());
					uint8_t *w2=pixels2.ptr();
					//vert
					for(int x=0;x<s.width;x++) {
						for(int y=0;y<s.height;y++) {

							int ofs = y*s.width+x;
							int sum=wa[ofs];

							for(int k=1;k<=r;k++) {

								int ofs_d=MIN(y+k,s.height-1)*s.width+x;
								int ofs_u=MAX(y-k,0)*s.width+x;
								sum+=wa[ofs_d];
								sum+=wa[ofs_u];
							}

							w2[ofs]=sum/(r*2+1);

						}
					}
					//horiz
					for(int x=0;x<s.width;x++) {
						for(int y=0;y<s.height;y++) {

							int ofs = y*s.width+x;
							int sum=w2[ofs];

							for(int k=1;k<=r;k++) {

								int ofs_r=MIN(x+k,s.width-1)+s.width*y;
								int ofs_l=MAX(x-k,0)+s.width*y;
								sum+=w2[ofs_r];
								sum+=w2[ofs_l];
							}

							wa[ofs]=Math::pow(float(sum/(r*2+1))/255.0,tr)*255.0;

						}
					}

				}

				//blend back

				for(int j=0;j<s.x*s.y;j++){
					Color wd(w[j*4+0]/255.0,w[j*4+1]/255.0,w[j*4+2]/255.0,w[j*4+3]/255.0);
					Color ws(sc.r,sc.g,sc.b,sc.a*(wa[j]/255.0));
					Color b = wd.blend(ws);

					w[j*4+0]=b.r*255.0;
					w[j*4+1]=b.g*255.0;
					w[j*4+2]=b.b*255.0;
					w[j*4+3]=b.a*255.0;

				}
			}
		}

		for(int y=0;y<oh;y++) {
			int yc=CLAMP(y+efd.valign,0,height-1);
			Color sc=color[yc];
			for(int x=0;x<ow;x++) {
				int ofs = (o.y+y)*s.x+x+o.x;
				float c = efd.bitmap[y*ow+x]/255.0;
				Color src_col=sc;
				src_col.a*=c;
				Color dst_col(w[ofs*4+0]/255.0,w[ofs*4+1]/255.0,w[ofs*4+2]/255.0,w[ofs*4+3]/255.0);
				dst_col = dst_col.blend(src_col);
				w[ofs*4+0]=dst_col.r*255.0;
				w[ofs*4+1]=dst_col.g*255.0;
				w[ofs*4+2]=dst_col.b*255.0;
				w[ofs*4+3]=dst_col.a*255.0;
			}
		}


		w=DVector<uint8_t>::Write();

		Image img(s.width,s.height,0,Image::FORMAT_RGBA,pixels);

		efd.blit=img;
		efd.blit_ofs=o;
    }

	//make atlas
	int spacing=2;
    Size2i font_size=Size2(efd.blit.get_width()+spacing*2,efd.blit.get_height()+spacing*2);
    Point2 blit_size(efd.blit.get_width(),efd.blit.get_height());

    if(blit_size.x>0&&blit_size.y>0)
    {
        if (p_atlas_x+blit_size.width+spacing>ALTAS_SIZE) {
            p_atlas_x=0;
            p_atlas_y+=p_atlas_height;
            p_atlas_height=0;
        }

        Image* atlas=NULL;
        if (p_images.empty()||(p_atlas_y+blit_size.height+spacing>ALTAS_SIZE)) {

            atlas=memnew( Image(ALTAS_SIZE,ALTAS_SIZE,0,Image::FORMAT_RGBA) );
            p_images.push_back(atlas);

            p_atlas_x=p_atlas_y=p_atlas_height=0;
        } else {

            int sz=p_images.size();
            atlas=p_images[sz-1];
        }
        atlas->blit_rect(efd.blit,Rect2(Point2(0,0),blit_size),Vector2(p_atlas_x,p_atlas_y)+Size2(spacing,spacing));
        if (p_atlas_height < (blit_size.y + spacing * 2))
            p_atlas_height=blit_size.y + spacing * 2;
        efd.texture=p_images.size()-1;
        efd.ofs_x=p_atlas_x+spacing;
        efd.ofs_y=p_atlas_y+spacing;
        p_atlas_x+=(blit_size.width+spacing*2);

	    if (p_options["color/monochrome"] && bool(p_options["color/monochrome"])) {

            atlas->convert(Image::FORMAT_GRAYSCALE_ALPHA);
	    }
        bool filter_enabled = p_options["filter/enabled"];
	    //register texures
        //tex->create_from_image( atlas );
        //if (!filter_enabled)
        //    tex->set_flags(Texture::FLAG_MIPMAPS | Texture::FLAG_REPEAT);
        //tex->set_storage( ImageTexture::STORAGE_COMPRESS_LOSSLESS );

        memdelete_arr(color);
    }
    ch->texture_idx=efd.texture;
    ch->rect=Rect2( efd.ofs_x, efd.ofs_y, efd.blit.get_width(), efd.blit.get_height());

	int char_space = p_options["extra_space/char"];
	int space_space = p_options["extra_space/space"];
	int top_space = p_options["extra_space/top"];
    int advance=efd.advance+char_space+(efd.character==' '?space_space:0);

    ch->h_align=efd.halign-efd.blit_ofs.x;
    ch->v_align=efd.valign-efd.blit_ofs.y+top_space;
    ch->advance=advance;

    return true;
}

void TtfFont::gen_kerning(Font *p_font) {

    for(int i=0;i<=512;i++) {
        for(int j=0;j<512;j++) {
			FT_Vector  delta;
            int left=FT_Get_Char_Index( face, i );
            int right=FT_Get_Char_Index( face, j );
			FT_Get_Kerning( face, left, right, FT_KERNING_DEFAULT, &delta );
			if (delta.x!=0) {
				int kern = ((-delta.x)+(1<<5))>>6;
				if (kern==0)
					continue;
                p_font->add_kerning_pair(left, right, kern);
			}
        }
    }
}

TtfFont::TtfFont() {
    face=NULL;

    if(instance_count++==0)
    {
    	int error = FT_Init_FreeType( &library );
    	ERR_EXPLAIN("Error initializing FreeType.");
	    ERR_FAIL_COND( error !=0 );
    }
}

TtfFont::~TtfFont() {
    if(--instance_count==0 && library!=NULL)
    {
	    FT_Done_FreeType( library );
        library=NULL;
    }
}

RES ResourceFormatLoaderTtfFont::load(const String &p_path,const String& p_original_path) {
    if (p_path.ends_with(".ttf") || p_path.ends_with(".otf")) {
        TtfFont *font=memnew( TtfFont );
        Ref<TtfFont> fontres(font);
        ERR_FAIL_COND_V(!font->load_ttf(p_path),RES());
	    return fontres;
    }
    return RES();
}

void ResourceFormatLoaderTtfFont::get_recognized_extensions(List<String> *p_extensions) const {
    p_extensions->push_back("ttf");
    p_extensions->push_back("otf");
}

bool ResourceFormatLoaderTtfFont::handles_type(const String& p_type) const {
	return p_type=="Font"||p_type=="TtfFont";
}

String ResourceFormatLoaderTtfFont::get_resource_type(const String &p_path) const {
    String el = p_path.extension().to_lower();
    if (el=="ttf" || el=="otf")
	    return "Font";
    return "";
}

#endif

