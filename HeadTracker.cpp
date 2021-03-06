#include "HeadTracker.h"
#include "FMatrixAffineCompute.cpp"
#include <fstream>

// Decide whether two numbers have the same sign
bool issamesign(int a, int b) { 
	if(a > 0 && b < 0)
		return false;
	
	if(a < 0 && b > 0)
		return false;
	
	return true;
}

static double squarenorm(Point point) {
    return square(point.x) + square(point.y);
}


static double mean(vector<Point> const& vec, 
		   double (Point::*prop)) 
{
    double sum = 0.0;
    for(int i=0; i<vec.size(); i++)
	    sum += (vec[i].*prop);
    return sum / vec.size();
}


static void predict(double xorig, double yorig, 
	     double a, double b, double c, double d, double depth,
	     double &xnew, double &ynew) 
{
    double A =       - a*xorig - b*yorig;
    double B = depth - b*xorig + a*yorig;

    xnew = (c*A + d*B) / (d*d + c*c);
    ynew = (d*A - c*B) / (d*d + c*c);
}
		 

static Point 
predictpoint(Point p, double depth, double dmeanx, double dmeany, 
	     double rotx, double roty, double atx, double aty) 
{ 
    Point p2 (p.x * atx - p.y * aty,
		 p.x * aty + p.y * atx);

    return Point(p2.x + roty * depth + dmeanx, 
 		    p2.y - rotx * depth + dmeany);
}

vector<bool> HeadTracker::detectinliers(vector<Point> const &prev, 
					vector<Point> const &now,
					double radius)
{
    assert(prev.size() == now.size());

    vector<Point> transitions;
    for(int i=0; i<prev.size(); i++)
	    transitions.push_back(now[i] - prev[i]);

//     cout << "xtrans:";
//     for(int i=0; i<prev.size(); i++)
// 	cout << " " << transitions[i].x;
//     cout << endl;

//     cout << "ytrans:";
//     for(int i=0; i<prev.size(); i++)
// 	cout << " " << transitions[i].y;
//     cout << endl;

    vector<int> closepoints(transitions.size());
    for(int i=0; i<transitions.size(); i++) {
	    closepoints[i] = 0;
	    for(int j=0; j<transitions.size(); j++)
	        if (squarenorm(transitions[i] - transitions[j]) <= square(radius))
		        closepoints[i]++;
    }

    int maxindex = max_element(closepoints.begin(), closepoints.end()) 
	- closepoints.begin();

    vector<bool> inliers(transitions.size());
    for(int i=0; i<transitions.size(); i++)
	    inliers[i] = (squarenorm(transitions[i] - transitions[maxindex]) 
		                    <= square(radius));

    for(int i=0; i<inliers.size(); i++)
	if (!inliers[i]) {
		tracker.status[i] = false;

  	    tracker.currentpoints[i].x = 
		0.3 * (tracker.origpoints[i].x + transitions[maxindex].x)
		+ 0.7 * tracker.currentpoints[i].x;
			//tracker.origpoints[i].x + transitions[maxindex].x;
			
  	    tracker.currentpoints[i].y = 
		0.3 * (tracker.origpoints[i].y + transitions[maxindex].y)
		+ 0.7 * tracker.currentpoints[i].y;
			//tracker.origpoints[i].y + transitions[maxindex].y;
	}

    return inliers;
}


void 
HeadTracker::predictpoints(double xx0, double yy0, double xx1, double yy1,
			   double rotx, double roty, double atx, double aty) 
{
    double maxdiff = 0.0;
    int diffindex = -1;

    vector<Point> points = 
	tracker.getpoints(&PointTracker::origpoints, true);

    for(int i=0; i<points.size(); i++) {
	Point p(points[i].x - xx0, points[i].y - yy0);
	Point p1 = predictpoint(p, depths[i], xx1, yy1, 
				   rotx, roty, atx, aty);
	Point p2 = predictpoint(p, -depths[i], xx1, yy1,
				   rotx, roty, atx, aty);

	double diff1 = fabs(p1.x - tracker.currentpoints[i].x) + 
	    fabs(p1.y - tracker.currentpoints[i].y);

	double diff2 = fabs(p2.x - tracker.currentpoints[i].x) + 
	    fabs(p2.y - tracker.currentpoints[i].y);

	double diff = diff1 > diff2 ? diff2 : diff1;
	// dubious code, I'm not sure about it
	if (!tracker.status[i]) {
		//cout << "P1 and P2: " << p1.x << ", " << p1.y << " - " << p2.x << ", " << p2.y << endl;
		//cout << "DEPTH: " << depths[i] << endl;
		//cout << "DIFFS: " << diff1 << ", " << diff2 << endl;
		
		if(diff == diff1) {
		    tracker.currentpoints[i].x = 
			0 * tracker.currentpoints[i].x + 1 * p1.x;
		    tracker.currentpoints[i].y = 
			0 * tracker.currentpoints[i].y + 1 * p1.y;
			//cout << "UPDATED WITH P1 POSITION" << endl;
		}
		else{
		    tracker.currentpoints[i].x = 
			0 * tracker.currentpoints[i].x + 1 * p2.x;
		    tracker.currentpoints[i].y = 
			0 * tracker.currentpoints[i].y + 1 * p2.y;
			cout << "UPDATED WITH P2 POSITION" << endl;
		}
	}
    }
}
void HeadTracker::updatetracker(void) {
	try{
	    depths.resize(tracker.pointcount());
	    detectinliers(tracker.getpoints(&PointTracker::origpoints, true), 
			  tracker.getpoints(&PointTracker::currentpoints, true));

    vector<Point> origpoints = 
	tracker.getpoints(&PointTracker::origpoints, false);
    vector<Point> currentpoints = 
	tracker.getpoints(&PointTracker::currentpoints, false);

    double xx0 = mean(origpoints, &Point::x);
    double yy0 = mean(origpoints, &Point::y);
    double xx1 = mean(currentpoints, &Point::x);
    double yy1 = mean(currentpoints, &Point::y);

	    vector<double>* fmatrix = computeAffineFMatrix(origpoints, currentpoints);
	
		if(fmatrix->empty()) {
			//cout << "Problem in computeAffineFMatrix" << endl;
			return;
		}
    
	    double a = (*fmatrix)[0];
	    double b = (*fmatrix)[1];
	    double c = (*fmatrix)[2];
	    double d = (*fmatrix)[3];
	    double e = (*fmatrix)[4];

    // compute the change

    vector<double> offsets(tracker.pointcount());

    double depthsum = 0.0001;
    double offsetsum = 0.0001;

    for(int i=0; i<tracker.pointcount(); i++) 
	if (tracker.status[i]) {
	    double xorig = tracker.origpoints[i].x - xx0;
	    double yorig = tracker.origpoints[i].y - yy0;
	    double xnew = tracker.currentpoints[i].x - xx1;
	    double ynew = tracker.currentpoints[i].y - yy1;
	    double x0 = b*xorig - a*yorig;
	    double x1 = -d*xnew + c*ynew;
	    offsets[i] = x0 - x1;
	    offsetsum += offsets[i]*offsets[i];
	    depthsum += depths[i]*depths[i];
	}
    
    if (tracker.areallpointsactive()) {
// 	cout << endl;
	depthsum = 1.0;
    }

    double depthscale = sqrt(offsetsum / depthsum);

    rotx = c * depthscale / hypot(a,b) / hypot(c,d);
    roty = d * depthscale / hypot(a,b) / hypot(c,d);

    atx = -(a*c + b*d) / (c*c + d*d); // at = AmpliTwist
    aty = -(a*d - c*b) / (c*c + d*d); // at = AmpliTwist

    // depths

    vector<double> newdepths(tracker.pointcount());

    for(int i=0; i<tracker.pointcount(); i++) 
	if (tracker.status[i]) 
	    newdepths[i] = offsets[i] / depthscale;

    if (newdepths[1] > newdepths[2]) {
	rotx = -rotx;
	roty = -roty;
	for(int i=0; i<tracker.pointcount(); i++)
	    depths[i] = -newdepths[i];
    }
    else
	for(int i=0; i<tracker.pointcount(); i++)
	    depths[i] = newdepths[i];
	

//     // distance
//     double distance1 = 0.0;
//     double distance2 = 0.0;
//     for(int i=0; i<tracker.pointcount; i++)
// 	if (tracker.status[i]) {
// 	    distance1 += square(depths[i] - newdepths[i]);
// 	    distance2 += square(depths[i] + newdepths[i]);
// 	}

//     for(int i=0; i<tracker.pointcount; i++) 
// 	if (tracker.status[i])
// 	    if (distance1 > distance2) {
// 		rotx = -rotx;
// 		roty = -roty;
// 		depths[i] = -newdepths[i];
// 	    }
//             else
// 		depths[i] = newdepths[i];
	
	    predictpoints(xx0, yy0, xx1, yy1,
	 		  rotx, roty, atx, aty);
	}
	catch (std::exception &ex) {
		cout << ex.what() << endl;
    }

}

void HeadTracker::draw(IplImage *image) {
//     cout << "state: "<< rotx << " " << roty << " " 
// 	 << atx << " " << aty << endl;
	
    cvLine(image, 
	   cvPoint(320, 240),
	   cvPoint(320 + int(atx * 50), 240 + int(aty * 50)),
	   CV_RGB(255,255,255));

//     for(int i=0; i<tracker.pointcount; i++)
// 	cvLine(image, 
// 	       cvPoint(tracker.currentpoints[i].x, tracker.currentpoints[i].y),
// 	       cvPoint(tracker.origpoints[i].x - xx0 + xx1, 
// 		       tracker.origpoints[i].y - yy0 + yy1), 
// 	       CV_RGB(255,0,0));
    
    for(int i=0; i<tracker.pointcount(); i++) {
	cvLine(image, 
	       cvPoint((int)tracker.currentpoints[i].x, 
		       (int)tracker.currentpoints[i].y),
	       cvPoint((int)tracker.currentpoints[i].x, 
		       int(tracker.currentpoints[i].y + depths[i] * 100)),
	       CV_RGB(0,0,255));
    }

    double scale = 10;

    cvLine(image, 
	   cvPoint(320, 240),
	   cvPoint(320 + int(roty * scale), 240 + int(rotx * scale)),
	   CV_RGB(255,0,0));    
}
