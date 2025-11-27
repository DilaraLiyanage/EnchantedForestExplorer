#pragma once
#include <GL/gl.h>

void drawCircle(int xc, int yc, int r) {
    glBegin(GL_POINTS);
    int x = 0, y = r;
    int d = 1 - r;

    while (x <= y) {
        glVertex2i(xc + x, yc + y);
        glVertex2i(xc - x, yc + y);
        glVertex2i(xc + x, yc - y);
        glVertex2i(xc - x, yc - y);
        glVertex2i(xc + y, yc + x);
        glVertex2i(xc - y, yc + x);
        glVertex2i(xc + y, yc - x);
        glVertex2i(xc - y, yc - x);

        if (d < 0) d += 2 * x + 3;
        else { d += 2 * (x - y) + 5; y--; }
        x++;
    }
    glEnd();
}
