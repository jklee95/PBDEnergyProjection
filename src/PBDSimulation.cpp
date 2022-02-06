#include "PBDSimulation.h"

using namespace DirectX;
using namespace std;
using namespace DXViewer::xmfloat2;

PBDSimulation::PBDSimulation(float timeStep)
	:_timeStep(timeStep)
{
	// Float initialization
	_nodeCount = { 10, 10 };
	_floorPosition = -2.0f * _nodeCount.y;
	_stride = 2.0f;
	_gravity = 9.8f;
	_lineCount = 50;

	// Vector initialization
	size_t vSize = static_cast<size_t>(_nodeCount.x) * static_cast<size_t>(_nodeCount.y);
	_newPosition.assign(vSize, { 0.0f, 0.0f });

	//_filePBD.open("filePBD2.txt");
}

PBDSimulation::~PBDSimulation()
{
	//_filePBD.close();
}


void PBDSimulation::_update()
{
	_solvePBD();
	if (_projFlag) _projectHamiltonian();
	//_filePBD << _computeHamiltonian() << endl;
}


void PBDSimulation::_initializeNode(std::vector<ConstantBuffer>& constantBuffer)
{
	// Body initialization
	for (int j = 0; j < _nodeCount.y; j++)
	{
		for (int i = 0; i < _nodeCount.x; i++)
		{
			float offsetX = 0.0f;
			float offsetY = 10.0f;

			float f_i = static_cast<float>(i);
			float f_j = static_cast<float>(j);

			XMFLOAT2 pos = 
			{ 
				offsetX + f_i * _stride, 
				offsetY + f_j * _stride
			};
			_nodePosition.push_back(pos);
			_nodeVelocity.push_back(XMFLOAT2(0.0f, 0.0f));

			ConstantBuffer objectCB;
			objectCB.world = DXViewer::util::transformMatrix(pos.x, pos.y, 0.0f, 1.0f);
			objectCB.worldViewProj = DXViewer::util::transformMatrix(0.0f, 0.0f, 0.0f);
			objectCB.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

			constantBuffer.push_back(objectCB);
		}
	}


	// Floor, Line initialization
	for (int i = 0; i < _lineCount * 2; i++)
	{
		float x = static_cast<float>(-_lineCount + i);
		ConstantBuffer floorCB, lineCB;

		floorCB.world = DXViewer::util::transformMatrix(
			x, _floorPosition - 1.0f, 0.0f, 1.0f);
		floorCB.worldViewProj = DXViewer::util::transformMatrix(0.0f, 0.0f, 0.0f);
		floorCB.color = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);

		lineCB.world = DXViewer::util::transformMatrix(
			x, 1.0f, 0.0f, 1.0f);
		lineCB.worldViewProj = DXViewer::util::transformMatrix(0.0f, 0.0f, 0.0f);
		lineCB.color = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);

		constantBuffer.push_back(floorCB);
		constantBuffer.push_back(lineCB);
	}
}

void PBDSimulation::_solvePBD()
{
	_newPosition = _nodePosition;
	for (SpringConstraint& sp : _constraint)
	{
		sp.setLambda(XMFLOAT2(0.0f, 0.0f));
	}

	float dt = _timeStep;
	int nSteps = 20;
	float subdt = dt / static_cast<float>(nSteps);

	for (int t = 0; t < nSteps; t++)
	{
		// External force
		for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
		{
			_newPosition[j].y += (subdt * _nodeVelocity[j].y) + (subdt * subdt * -_gravity);
		}

		// Constraint projection
		for (int iter = 0; iter < 10; iter++)
		{
			for (SpringConstraint& sp : _constraint)
			{
				sp.projectConstraint(subdt);
			}

			// Floor boundary condition
			for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
			{
				if (_newPosition[j].y < _floorPosition)
					_newPosition[j].y = _floorPosition;
			}
		}

		// Update the velocity
		for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
		{
			_nodeVelocity[j] = (_newPosition[j] - _nodePosition[j]) / subdt;
			_nodePosition[j] = _newPosition[j];
		}

	}
	
}

float PBDSimulation::_computeHamiltonian()
{
	float H = 0.0f;

	for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
	{
		H += 0.5 * (_nodeVelocity[j] * _nodeVelocity[j]);					// Kinetic energy
		H += _gravity * (_nodePosition[j].y - _floorPosition);				// Potential energy
	}

	for (SpringConstraint& sp : _constraint)
	{
		H += sp.computeElasticEnergy();										// Elastic energy
	}

	return H;
}

float PBDSimulation::_computeHamiltonianGradient(GradType gradType, int j)
{
	float gradH = 0.0f;
	switch (gradType)
	{
	case GradType::V:
		gradH += _nodeVelocity[j].x + _nodeVelocity[j].y;
		break;

	case GradType::X:
		gradH += _gravity;

		for (SpringConstraint& sp : _constraint)
		{
			if (sp.getP1Index() == j)
				gradH += sp.computeElasticEnergyGradient(j);
			else if (sp.getP2Index() == j)
				gradH -= sp.computeElasticEnergyGradient(j);
		}
		break;
	}

	return gradH;
}

void PBDSimulation::_projectHamiltonian()
{
	float H = _hamiltonian;
	float delta_x = 0.0f, delta_v = 0.0f;
	float new_H;
	float grad_Hx, grad_Hv;
	float pow_grad_Hx = 0.0f, pow_grad_Hv = 0.0f;
	float h = _timeStep;

	for (int iter = 0; iter < 1; iter++)
	{
		new_H = _computeHamiltonian();

		for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
		{
			grad_Hx = _computeHamiltonianGradient(GradType::X, j);
			grad_Hv = _computeHamiltonianGradient(GradType::V, j);

			pow_grad_Hx += pow(grad_Hx, 2.0f);
			pow_grad_Hv += (1.0f / pow(h, 2.0f)) * pow(grad_Hv, 2.0f);

		}

		for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
		{
			grad_Hx = _computeHamiltonianGradient(GradType::X, j);
			grad_Hv = _computeHamiltonianGradient(GradType::V, j);

			delta_x = grad_Hx * (-new_H + H) / (pow_grad_Hx + pow_grad_Hv);
			delta_v = (1.0f / pow(h, 2.0f)) * grad_Hv * (-new_H + H) / (pow_grad_Hx + pow_grad_Hv);

			_nodePosition[j] = _nodePosition[j] + delta_x;
			_nodeVelocity[j] = _nodeVelocity[j] + delta_v;

			// Floor boundary condition
			for (int j = 0; j < _nodeCount.x * _nodeCount.y; j++)
			{
				if (_nodePosition[j].y < _floorPosition)
					_nodePosition[j].y = _floorPosition;
			}

		}
	}
}


#pragma region Implementation
// ################################## Implementation ####################################
// Simulation methods
void PBDSimulation::iUpdate()
{
	for (int i = 0; i < 1; i++)
	{
		_update();
	}
}

void PBDSimulation::iResetSimulationState(std::vector<ConstantBuffer>& constantBuffer)
{
	_nodePosition.clear();
	_nodeVelocity.clear();
	constantBuffer.clear();

	_initializeNode(constantBuffer);
}


// Mesh methods
vector<Vertex>& PBDSimulation::iGetVertice()
{
	_vertices =
	{
		Vertex({ XMFLOAT3(-0.5f, -0.5f, 0.0f) }),
		Vertex({ XMFLOAT3(-0.5f, +0.5f, 0.0f) }),
		Vertex({ XMFLOAT3(+0.5f, +0.5f, 0.0f) }),
		Vertex({ XMFLOAT3(+0.5f, -0.5f, 0.0f) })
	};

	return _vertices;
}

vector<unsigned int>& PBDSimulation::iGetIndice()
{
	_indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,
	};

	return _indices;
}

UINT PBDSimulation::iGetVertexBufferSize()
{
	return 4;
}

UINT PBDSimulation::iGetIndexBufferSize()
{
	return 6;
}


// DirectX methods
void PBDSimulation::iCreateObject(std::vector<ConstantBuffer>& constantBuffer)
{
	// Node initialization
	_initializeNode(constantBuffer);

	// Constraint initialization
	float alpha = 0.001f;
	for (int j = 0; j < _nodeCount.x * _nodeCount.y - 1; j++)
	{
		for (int i = j + 1; i < _nodeCount.x * _nodeCount.y; i++)
		{
			XMFLOAT2& curr_p1 = _nodePosition[i];
			XMFLOAT2& curr_p2 = _nodePosition[j];
			XMFLOAT2& new_p1 = _newPosition[i];
			XMFLOAT2& new_p2 = _newPosition[j];
			XMFLOAT2 d = fabsxmf2(_nodePosition[j] - _nodePosition[i]);
			float dist = sqrtf(d.x * d.x + d.y * d.y);

			if (dist < 1.5f * _stride)
			{
				SpringConstraint sp(curr_p1, curr_p2, new_p1, new_p2, i, j, d, alpha);
				_constraint.push_back(sp);
			}
		}
		
	}

	// Hamiltonian initialization
	_hamiltonian = _computeHamiltonian();

}

void PBDSimulation::iUpdateConstantBuffer(std::vector<ConstantBuffer>& constantBuffer, int i)
{
	int nodeCount = _nodeCount.x * _nodeCount.y;

	if (i < nodeCount)
	{
		constantBuffer[i].world._41 = _nodePosition[i].x;
		constantBuffer[i].world._42 = _nodePosition[i].y;
	}
}

void PBDSimulation::iDraw(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& mCommandList, int size, UINT indexCount, int i)
{
	mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void PBDSimulation::iSetDXApp(DX12App* dxApp)
{
	_dxapp = dxApp;
}

UINT PBDSimulation::iGetConstantBufferSize()
{
	return 
		_nodeCount.x * _nodeCount.y		// Body
		+ (_lineCount * 2)				// Floor
		+ (_lineCount * 2);				// Line
}

DirectX::XMINT3 PBDSimulation::iGetObjectCount()
{
	return { _nodeCount.x, _nodeCount.y, 0 };
}

DirectX::XMFLOAT3 PBDSimulation::iGetObjectSize()
{
	return { _stride, _stride, 0.0f };
}

DirectX::XMFLOAT3 PBDSimulation::iGetObjectPositionOffset()
{
	return { 0.0f, 0.0f, 0.0f };
}

bool PBDSimulation::iIsUpdated()
{
	return _updateFlag;
}


void PBDSimulation::iWMCreate(HWND hwnd, HINSTANCE hInstance)
{
	CreateWindow(L"button", _projFlag ? L"Proj : ON " : L"Proj : OFF ", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		90, 30, 100, 25, hwnd, reinterpret_cast<HMENU>(COM::PROJ_BTN), hInstance, NULL);

	CreateWindow(L"button", _updateFlag ? L"��" : L"��", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		65, 305, 50, 25, hwnd, reinterpret_cast<HMENU>(COM::PLAY), hInstance, NULL);
	CreateWindow(L"button", L"��", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		115, 305, 50, 25, hwnd, reinterpret_cast<HMENU>(COM::STOP), hInstance, NULL);
	CreateWindow(L"button", L"��l", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		165, 305, 50, 25, hwnd, reinterpret_cast<HMENU>(COM::NEXTSTEP), hInstance, NULL);

	/*CreateWindow(L"static", L"time :", WS_CHILD | WS_VISIBLE,
		95, 350, 40, 20, hwnd, reinterpret_cast<HMENU>(-1), hInstance, NULL);
	CreateWindow(L"static", to_wstring(_simTime).c_str(), WS_CHILD | WS_VISIBLE,
		140, 350, 40, 20, hwnd, reinterpret_cast<HMENU>(COM::TIME_TEXT), hInstance, NULL);
	CreateWindow(L"static", L"frame :", WS_CHILD | WS_VISIBLE,
		86, 370, 45, 20, hwnd, reinterpret_cast<HMENU>(-1), hInstance, NULL);
	CreateWindow(L"static", to_wstring(_simFrame).c_str(), WS_CHILD | WS_VISIBLE,
		140, 370, 40, 20, hwnd, reinterpret_cast<HMENU>(COM::FRAME_TEXT), hInstance, NULL);*/

	if (_updateFlag)
	{
		EnableWindow(GetDlgItem(hwnd, static_cast<int>(COM::NEXTSTEP)), false);
	}
}

void PBDSimulation::iWMCommand(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, HINSTANCE hInstance)
{
	switch (LOWORD(wParam))
	{
		case static_cast<int>(COM::PROJ_BTN):
		{
			_projFlag = !_projFlag;
			SetDlgItemText(hwnd, static_cast<int>(COM::PROJ_BTN), _projFlag ? L"Proj : ON " : L"Proj : OFF");
			_dxapp->resetSimulationState();
		}
		break;

		// ### Execution buttons ###
		case static_cast<int>(COM::PLAY):
		{
			_updateFlag = !_updateFlag;
			SetDlgItemText(hwnd, static_cast<int>(COM::PLAY), _updateFlag ? L"��" : L"��");

			EnableWindow(GetDlgItem(hwnd, static_cast<int>(COM::STOP)), true);
			EnableWindow(GetDlgItem(hwnd, static_cast<int>(COM::NEXTSTEP)), !_updateFlag);
		}
		break;
		case static_cast<int>(COM::STOP):
		{
			_dxapp->resetSimulationState();
		}
		break;
		case static_cast<int>(COM::NEXTSTEP):
		{
			iUpdate();
			_dxapp->update();
			_dxapp->draw();
		}
		break;
		// #####################
	}
}

void PBDSimulation::iWMHScroll(HWND hwnd, WPARAM wParam, LPARAM lParam, HINSTANCE hInstance)
{
}

void PBDSimulation::iWMTimer(HWND hwnd)
{
}

void PBDSimulation::iWMDestory(HWND hwnd)
{
}
// #######################################################################################
#pragma endregion