//
//  sine.h
//  cubesquare
//
//  Created by Eric Kampman on 6/24/19.
//

#ifndef sine_h
#define sine_h

#include <stdint.h>

extern const double sinValues[];
extern const double sin1_2Values[];
extern const double sin1_3Values[];
extern const double sin1_3_5Values[];
extern const double sin1_2_3_4Values[];
extern const double sinValueMultiplier;

double sinTableVal(double f, double unused);
double sin1_2TableVal(double f);
double sin1_3TableVal(double f);
double sin1_3_5TableVal(double f);
double sin1_2_3_4TableVal(double f);

//void makeSinTable(uint32_t denom);

#endif /* sine_h */
