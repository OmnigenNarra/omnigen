struct ShorePoint
{
    float x;
    float z;
    // type: -1 = left; 0 = straight; 1 = right
    int forwardType;
    int backwardType;
    float2 forwardDir;
    float2 backwardDir;
    int forwardPair;
    int backwardPair;
};

RWStructuredBuffer<ShorePoint> pts : register(u0);
static const float e = 0.00001f;

bool feq(float a, float b)
{
    bool result = false;
    float diff = abs(a - b);

    if (diff <= e)
        result = true;

    if (diff <= (e * max(abs(a), abs(b))))
        result = true;

    return result;
}

void computePointType(uint i)
{
    float2 prev = float2(pts[i - 1].x, pts[i - 1].z);
    float2 here = float2(pts[i].x, pts[i].z);
    float2 next = float2(pts[i + 1].x, pts[i + 1].z);

    float2 seg1 = here - prev;
    float2 seg2 = next - here;
    pts[i].forwardDir = normalize(seg1);
    pts[i].backwardDir = normalize(-seg2);

    // There is no turn.
    if (feq(prev.x, next.x) || feq(prev.y, next.y))
    {
        pts[i].forwardType = 0;
        pts[i].backwardType = 0;
        return;
    }

    float2 leftOf1 = float2(-seg1.y, seg1.x);
    if (feq(seg2.x, 0.0f))
    {
        if (feq(seg2.y, leftOf1.y))
        {
            pts[i].forwardType = -1;
            pts[i].backwardType = 1;
        }
        else
        {
            pts[i].forwardType = 1;
            pts[i].backwardType = -1;
        }
    }
    else // seg2.y == 0.0f
    {
        if (feq(seg2.x, leftOf1.x))
        {
            pts[i].forwardType = -1;
            pts[i].backwardType = 1;
        }
        else
        {
            pts[i].forwardType = 1;
            pts[i].backwardType = -1;
        }
    }
}

void detect(uint i, uint size, bool forward)
{
    // which segment to extend?
    // forward: i-1 to i
    // backward: i+1 to i
    float2 targetDir = forward ? pts[i].forwardDir : pts[i].backwardDir;

    int target = -1;
    float minD = -1;

    for (uint p = 0; p < size; ++p)
    {
        // ignore self
        if (p == i)
            continue;

        /////////////////////////////////////////////////////////////////////
        // link from i to p
        float2 link = float2(pts[p].x - pts[i].x, pts[p].z - pts[i].z);
        float2 linkN = normalize(link);

        // link direction must match
        if (!feq(linkN.x, targetDir.x) || !feq(linkN.y, targetDir.y))
            continue;

        /////////////////////////////////////////////////////////////////////
        // manhattan distance
        float d = abs(pts[p].x - pts[i].x) + abs(pts[p].z - pts[i].z);

        // link must have the minimal distance
        if (minD > 0 && d > minD)
            continue;

        /////////////////////////////////////////////////////////////////////
        // All good here
        target = p;
        minD = d;
    }
        
    if (target == -1)
        return;

    /////////////////////////////////////////////////////////////////////
    // link from p-1 to p
    float2 l1 = float2(pts[target].x - pts[target - 1].x, pts[target].z - pts[target - 1].z);
    float2 l1N = normalize(l1);

    // l1 direction can't match
    if (feq(l1N.x, targetDir.x) && feq(l1N.y, targetDir.y))
        return;

    /////////////////////////////////////////////////////////////////////
    // link from p+1 to p
    float2 l2 = float2(pts[target].x - pts[target + 1].x, pts[target].z - pts[target + 1].z);
    float2 l2N = normalize(l2);

    // l1 direction can't match
    if (feq(l2N.x, targetDir.x) && feq(l2N.y, targetDir.y))
        return;

    /////////////////////////////////////////////////////////////////////
    // Bay diagonal must be at most 30% of along-shore distance.
    float segmentLength = abs(pts[0].x - pts[1].x) + abs(pts[0].z - pts[1].z);
    float shoreD = abs(int(i) - int(target)) * segmentLength;
    if (minD > 0.3f * shoreD)
        return;

    if (forward)
    {
        pts[i].forwardPair = target;
    }
    else
    {
        pts[i].backwardPair = target;
    }
}

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint size, stride;
    pts.GetDimensions(size, stride);

    uint i = DTid.x;
    pts[i].forwardPair = -1;
    pts[i].backwardPair = -1;

    if ((i == 0) || (i == size - 1))
        return;

    computePointType(i);

    detect(i, size, true);
    detect(i, size, false);
}