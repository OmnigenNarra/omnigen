struct Unit
{
	int value;
};

StructuredBuffer<Unit> Buffer0 : register(t0);
RWStructuredBuffer<Unit> BufferOut : register(u0);

int readValue(int x, int y)
{
	uint index = (x + y * 1024);
	return Buffer0[index].value;
}

void writeToValue(int x, int y, int newValue)
{
	uint index = (x + y * 1024);
	BufferOut[index].value = newValue;
}

[numthreads(32, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int inValue = readValue(dispatchThreadID.x, dispatchThreadID.y);
	writeToValue(dispatchThreadID.x, dispatchThreadID.y, inValue * inValue);
}