#pragma once
#include <cstdint>

// https://xantorohara.github.io/led-matrix-editor/#894218a425184291|09080d7a8a9d7809|405ea12939ce2023|00007e818999710e|152a547e8191710e|152a547e8191710e|0a04087e8191710e|00ff003e00fc003f|a5429924249942a5

const uint8_t IMAGES[][9] = {
{ // Clear sky - 01d
  0b10001001,
  0b01000010,
  0b00011000,
  0b10100100,
  0b00100101,
  0b00011000,
  0b01000010,
  0b10010001
},{ // few clouds - 02d
  0b10010000,
  0b00011110,
  0b10111001,
  0b01010001,
  0b01011110,
  0b10110000,
  0b00010000,
  0b10010000
},{ // scattered clouds - 03d
  0b11000100,
  0b00000100,
  0b01110011,
  0b10011100,
  0b10010100,
  0b10000101,
  0b01111010,
  0b00000010
},{ // broken clouds - 04d
  0b01110000,
  0b10001110,
  0b10011001,
  0b10010001,
  0b10000001,
  0b01111110,
  0b00000000,
  0b00000000
},{ // shower rain - 09d
  0b01110000,
  0b10001110,
  0b10001001,
  0b10000001,
  0b01111110,
  0b00101010,
  0b01010100,
  0b10101000
},{ // rain - 10d
  0b01110000,
  0b10001110,
  0b10001001,
  0b10000001,
  0b01111110,
  0b00101010,
  0b01010100,
  0b10101000
},{ // thunderstorm - 11d
  0b01110000,
  0b10001110,
  0b10001001,
  0b10000001,
  0b01111110,
  0b00010000,
  0b00100000,
  0b01010000
},{ // mist - 50d
  0b11111100,
  0b00000000,
  0b00111111,
  0b00000000,
  0b01111100,
  0b00000000,
  0b11111111,
  0b00000000
},{ // snow - 13d
  0b10100101,
  0b01000010,
  0b10011001,
  0b00100100,
  0b00100100,
  0b10011001,
  0b01000010,
  0b10100101
}};
const int IMAGES_LEN = sizeof(IMAGES)/9;

// Example response from OpenWeatherMap API:
// see example.json

typedef struct {
  char id[4];
  char name[64];
} weatherIcon;

weatherIcon APIData[18] = {
    {"01d", "Clear"},
    {"02d", "Few_clouds"},
    {"03d", "Scattered_clouds"},
    {"04d", "Broken_clouds"},
    {"09d", "Shower_rain"},
    {"10d", "Rain"},
    {"11d", "Thunderstorm"},
    {"50d", "Mist"},
    {"13d", "Snow"},
    {"01n", "Clear"},
    {"02n", "Few_clouds"},
    {"03n", "Scattered_clouds"},
    {"04n", "Broken_clouds"},
    {"09n", "Shower_rain"},
    {"10n", "Rain"},
    {"11n", "Thunderstorm"},
    {"50n", "Mist"},
    {"13n", "Snow"}
};


// Mapping OpenWeatherMap weather codes to images
// Group 2xx: Thunderstorm
// 200 	Thunderstorm 	thunderstorm with light rain 	11d
// 201 	Thunderstorm 	thunderstorm with rain 	11d
// 202 	Thunderstorm 	thunderstorm with heavy rain 	11d
// 210 	Thunderstorm 	light thunderstorm 	11d
// 211 	Thunderstorm 	thunderstorm 	11d
// 212 	Thunderstorm 	heavy thunderstorm 	11d
// 221 	Thunderstorm 	ragged thunderstorm 	11d
// 230 	Thunderstorm 	thunderstorm with light drizzle 	11d
// 231 	Thunderstorm 	thunderstorm with drizzle 	11d
// 232 	Thunderstorm 	thunderstorm with heavy drizzle 	11d
// Group 3xx: Drizzle
// 300 	Drizzle 	light intensity drizzle 	09d
// 301 	Drizzle 	drizzle 	09d
// 302 	Drizzle 	heavy intensity drizzle 	09d
// 310 	Drizzle 	light intensity drizzle rain 	09d
// 311 	Drizzle 	drizzle rain 	09d
// 312 	Drizzle 	heavy intensity drizzle rain 	09d
// 313 	Drizzle 	shower rain and drizzle 	09d
// 314 	Drizzle 	heavy shower rain and drizzle 	09d
// 321 	Drizzle 	shower drizzle 	09d
// Group 5xx: Rain
// 500 	Rain 	light rain 	10d
// 501 	Rain 	moderate rain 	10d
// 502 	Rain 	heavy intensity rain 	10d
// 503 	Rain 	very heavy rain 	10d
// 504 	Rain 	extreme rain 	10d
// 511 	Rain 	freezing rain 	13d
// 520 	Rain 	light intensity shower rain 	09d
// 521 	Rain 	shower rain 	09d
// 522 	Rain 	heavy intensity shower rain 	09d
// 531 	Rain 	ragged shower rain 	09d
// Group 6xx: Snow
// 600 	Snow 	light snow 	13d
// 601 	Snow 	snow 	13d
// 602 	Snow 	heavy snow 	13d
// 611 	Snow 	sleet 	13d
// 612 	Snow 	light shower sleet 	13d
// 613 	Snow 	shower sleet 	13d
// 615 	Snow 	light rain and snow 	13d
// 616 	Snow 	rain and snow 	13d
// 620 	Snow 	light shower snow 	13d
// 621 	Snow 	shower snow 	13d
// 622 	Snow 	heavy shower snow 	13d
// Group 7xx: Atmosphere
// 701 	Mist 	mist 	50d
// 711 	Smoke 	smoke 	50d
// 721 	Haze 	haze 	50d
// 731 	Dust 	sand/dust whirls 	50d
// 741 	Fog 	fog 	50d
// 751 	Sand 	sand 	50d
// 761 	Dust 	dust 	50d
// 762 	Ash 	volcanic ash 	50d
// 771 	Squall 	squalls 	50d
// 781 	Tornado 	tornado 	50d
// Group 800: Clear
// 800 	Clear 	clear sky 	01d 01n
// Group 80x: Clouds
// 801 	Clouds 	few clouds: 11-25% 	02d 02n
// 802 	Clouds 	scattered clouds: 25-50% 	03d 03n
// 803 	Clouds 	broken clouds: 51-84% 	04d 04n
// 804 	Clouds 	overcast clouds: 85-100% 	04d 04n

