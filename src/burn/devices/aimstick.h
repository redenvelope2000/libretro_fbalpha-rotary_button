//
// Support routines for the AIM STICK function.
//

#define ABS(A) (A<0?-A:A)
#define DEAD_ZONE_AIM_STICK 5000
#define FLOAT_ZONE_AIM_STICK 30.0

static double angle_between(double x1, double y1, double x2, double y2) {
    double d = atan2(x2, y2) - atan2(x1, y1);
    if (d < 0) d += 2 * M_PI;
    return d; // this is the angle between the two points in radians
}
static double degree_angle(double x1, double y1, double x2, double y2) {
    double angle = angle_between(x1, y1, x2, y2);
    return (angle * 180.0 / M_PI); // this converts from radians to degrees
}
static int aim_angle(int x1, int y1, int steps) {
    double angle = degree_angle(x1, y1, 0.0, 1.0);
    int fa = round (angle * steps/360.0);
    if (fa == steps) fa = 0;
    return fa;
}
static bool aim_stick_range(int x, int y, int *d) {
		int dis = x*x + y*y;
		int prev_dis = *d;
		double dr = prev_dis? ((float)(dis-prev_dis))/prev_dis*100.0 :0.0;
		*d = dis;
		if (dis > DEAD_ZONE_AIM_STICK*DEAD_ZONE_AIM_STICK && ABS(dr) < FLOAT_ZONE_AIM_STICK) {
			return true;
		}
		return false;
}
