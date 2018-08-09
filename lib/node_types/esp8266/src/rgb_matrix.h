// rgb_matrix.h
// rgb_matrix class for doing collection of rgb-strips and rgb leds

#ifndef _RGB_MATRIX_H_
#define _RGB_MATRIX_H_

#include <device.h>
#include <rgb_base.h>

enum Strip_Direction { Left_Down = 1, Left_Up, Right_Down, Right_Up, 
                    Down_Right, Down_Left, Up_Right, Up_Left};

typedef struct {
    int16_t stripnr;
    int16_t lednr;
} Strip_Address;

class RGB_Matrix : public RGB_Base {
    private:
        Strip_Address* matrix;
        RGB_Base* strips[ULNOIOT_MAX_LED_STRIPS];
        int height = 0;
        int width = 0;
        int strip_count = 0;
        int _fps;
        unsigned long frame_len;
        unsigned long last_frame=millis();

        Strip_Address* matrix_get( int x, int y ) {
            return matrix + width*y + x;
        }

        void matrix_set(int x, int y, int stripnr, int lednr) {
            if(x>=0 && y>=0 && x<width && y<height) {
                Strip_Address* pos = matrix_get(x,y);
                pos->stripnr = stripnr;
                pos->lednr = lednr;
            } else {
                log("Wrong position %d,%d for matrix requested.",x,y);
            }
        }

        void init(int w, int h) {
            width = w;
            height = h;
            matrix = (Strip_Address*)
                malloc(width*height*sizeof(Strip_Address)); // TODO: destructor?
            for(int y=0; y<height; y++)
                for(int x=0; x<width; x++)
                    matrix_set(x,y,-1,-1);
        }

    public:
        RGB_Matrix(const char* name, int _width, int _height) : RGB_Base(name,_width) {
            init(_width,_height);            
        }
        RGB_Matrix(const char* name, RGB_Base& strip) : RGB_Base(name,strip.led_count()) {
            init(strip.led_count(),1);
            add(strip, 0, 0);
        }
        RGB_Matrix& add(RGB_Base& strip, int posx, int posy, 
            Strip_Direction direction=Right_Down, int linelen=-1);
        RGB_Matrix& with(RGB_Base& strip, int posx, int posy, 
            Strip_Direction direction=Right_Down, int linelen=-1) {
            add(strip, posx, posy, direction, linelen);
            return *this;
        }
        int get_width() {
            return width;
        }
        int get_height() {
            return height;
        }
        bool measure();
        CRGB get_pixel(int x, int y, bool wrap=true) {
            Strip_Address *a;
            if(wrap) {
                x+=4*width;
                y+=4*height;
                a = matrix_get(x%width,y%height);
            } else {
                if(x<0 || y<0 || x>=width || y>=width) return CRGB(CRGB::Black);
                a = matrix_get(x,y);
            }
            if(a->stripnr >= 0) return strips[a->stripnr]->get_color(a->lednr);
            log("get_pixel, out of bounds: %d,%d",x,y); // TODO: remove debug
            return CRGB(CRGB::Black); // if something goes wrong, return black
        }
        CRGB set_pixel(int x, int y, CRGB color, bool wrap=true) {
            Strip_Address *a;
            if(wrap) {
                x+=4*width;
                y+=4*height;
                a = matrix_get(x%width,y%height);
            } else {
                if(x<0 || y<0 || x>=width || y>=width) return CRGB(CRGB::Black);
                a = matrix_get(x,y);
            }
            if(a->stripnr >= 0) {
                CRGB old = strips[a->stripnr]->get_color(a->lednr); 
                strips[a->stripnr]->set_color(a->lednr, color, false);
                return old;
            }
            log("set_pixel, out of bounds: %d,%d",x,y);  // TODO: remove debug
            return CRGB(CRGB::Black); // if something goes wrong, return black
        }
        // Matrix drawing functions
        void scroll_up(bool cycle=false, int column=-1);
        void scroll_down(bool cycle=false, int column=-1);
        void scroll_left(bool cycle=false, int line=-1);
        void scroll_right(bool cycle=false, int line=-1);
        // all off
        void black() {
            for(int y=0; y<height; y++) {
                for(int x=0; x<width; x++) {
                    set_pixel(x,y,CRGB::Black,false);
                }
            }
        }
        void off() { black(); }

        void rainbow(int startx=0, int starty=0,
                int w=-1, int h=-1,
                uint8_t initialhue=16, uint8_t deltahue=16 );

        void rainbow_row(int startx=0, int starty=0,
                int w=-1, int h=-1,
                uint8_t initialhue=16, uint8_t deltahue=16 );

        void rainbow_column(int startx=0, int starty=0,
                int w=-1, int h=-1,
                uint8_t initialhue=16, uint8_t deltahue=16 );

        void gradient_row( CRGB startcolor, CRGB endcolor,
                   int startx=0, int starty=0,
                   int w=-1, int h=-1 );

        void gradient_column( CRGB startcolor, CRGB endcolor,
                   int startx=0, int starty=0,
                   int w=-1, int h=-1 );

        void fade( uint8_t scale=4,
                int startx=0, int starty=0,
                int w=-1, int h=-1);

        virtual void process_color(int lednr, CRGB color, bool _show=true) {
            // interpret lednr as column
            if(lednr == ALL_LEDS) {
                gradient_column(color,color);
            } else {
                if(lednr<0) {
                    // push from left
                } else if(lednr>=width) {
                    // push from right
                } else {
                    // normal column access
                    gradient_column(color, color, lednr, 0, 1, -1);
                }
            }
        }

        virtual CRGB get_color(int lednr) {
            // TODO: think later if we can return here something smarter than black
            return CRGB::Black;
        }

        virtual void show() {
            for(int i=0; i<strip_count; i++)
                strips[i]->show();
        }
};


#endif // _RGB_MATRIX_H_