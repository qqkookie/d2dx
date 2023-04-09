/*
	This file is part of D2DX.

	Copyright (C) 2021  Bolrog

	D2DX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	D2DX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with D2DX.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "pch.h"
#include "TextMotionPredictor.h"

using namespace d2dx;
using namespace DirectX;

const float ROOT_TWO = 1.41421356237f;
const OffsetF GAME_TO_SCREEN_POS = { 32.f / ROOT_TWO, 16.f / ROOT_TWO };
const OffsetF TEXT_SEARCH_DIST = OffsetF(1.5f, 1.5f) * GAME_TO_SCREEN_POS;
const float TEXT_MIN_DELTA_LEN = TEXT_SEARCH_DIST.Length() / 2.5f;

_Use_decl_annotations_
TextMotionPredictor::TextMotionPredictor(
	const std::shared_ptr<IGameHelper>& gameHelper) :
	_gameHelper{ gameHelper },
	_textMotions{ 128, true },
	_textsCount{ 0 },
	_frame{ 0 }
{
}

_Use_decl_annotations_
void TextMotionPredictor::Update(
	IRenderContext* renderContext)
{
	renderContext->GetCurrentMetrics(&_gameSize, nullptr, nullptr);

	const float dt = renderContext->GetFrameTime();
	int32_t expiredTextIndex = -1;

	for (int32_t i = 0; i < _textsCount; ++i)
	{
		TextMotion& tm = _textMotions.items[i];

		if (!tm.textAddress)
		{
			expiredTextIndex = i;
			continue;
		}

		if (abs((int64_t)_frame - (int64_t)tm.lastUsedFrame) > 2)
		{
			tm.textAddress = 0;
			expiredTextIndex = i;
			continue;
		}

		OffsetF targetVec = tm.targetPos - tm.currentPos;
		float targetDistance = targetVec.Length();
		targetVec.Normalize();

		float step = min(dt * 30.0f * targetDistance, targetDistance);

		auto moveVec = targetVec * step;

		tm.currentPos += moveVec;
	}

	// Gradually (one change per frame) compact the list.
	if (_textsCount > 0)
	{
		if (!_textMotions.items[_textsCount - 1].textAddress)
		{
			// The last entry is expired. Shrink the list.
			_textMotions.items[_textsCount - 1] = { };
			--_textsCount;
		}
		else if (expiredTextIndex >= 0 && expiredTextIndex < (_textsCount - 1))
		{
			// Some entry is expired. Move the last entry to that place, and shrink the list.
			_textMotions.items[expiredTextIndex] = _textMotions.items[_textsCount - 1];
			_textMotions.items[_textsCount - 1] = { };
			--_textsCount;
		}
	}

	++_frame;
}

_Use_decl_annotations_
Offset TextMotionPredictor::GetOffset(
	uintptr_t textAddress,
	uint32_t textHash,
	Offset posFromGame)
{
	OffsetF posFromGameF(posFromGame);
	int32_t textIndex = -1;
	int32_t possibleIndex = -1;
	float possibleDelta = 100.f;
	bool multiplePossible = false;

	for (int32_t i = 0; i < _textsCount; ++i)
	{
		if (_textMotions.items[i].textHash == textHash)
		{
			if (_textMotions.items[i].textAddress == textAddress)
			{
				if ((_gameHelper->ScreenOpenMode() & 1) && posFromGameF.x >= _gameSize.width / 2 ||
					(_gameHelper->ScreenOpenMode() & 2) && posFromGameF.x <= _gameSize.width / 2)
				{
					_textMotions.items[i].currentPos = posFromGameF;
				}
				else
				{
					Offset delta = _textMotions.items[i].gamePos - posFromGame;
					if (std::abs(delta.x) > TEXT_SEARCH_DIST.x ||
						std::abs(delta.y) > TEXT_SEARCH_DIST.y)
					{
						_textMotions.items[i].currentPos = posFromGameF;
					}
				}

				_textMotions.items[i].targetPos = posFromGameF;
				_textMotions.items[i].lastUsedFrame = _frame;
				_textMotions.items[i].gamePos = posFromGame;
				textIndex = i;
				break;
			}
			else
			{
				Offset delta = _textMotions.items[i].gamePos - posFromGame;
				if (std::abs(delta.x) <= TEXT_SEARCH_DIST.x &&
					std::abs(delta.y) <= TEXT_SEARCH_DIST.y)
				{
					float lenDelta = OffsetF(delta).Length();
					if (possibleIndex == -1 ||
						(lenDelta < possibleDelta && possibleDelta - lenDelta >= TEXT_MIN_DELTA_LEN))
					{
						multiplePossible = false;
						possibleDelta = lenDelta;
						possibleIndex = i;
					}
					else {
						multiplePossible = true;
						possibleDelta = min(possibleDelta, lenDelta);
					}
				}
			}
		}
	}

	if (textIndex < 0)
	{
		if (possibleIndex >= 0 && !multiplePossible)
		{
			textIndex = possibleIndex;
			_textMotions.items[possibleIndex].textAddress = textAddress;
			_textMotions.items[possibleIndex].targetPos = posFromGameF;
			_textMotions.items[possibleIndex].gamePos = posFromGame;
			_textMotions.items[possibleIndex].lastUsedFrame = _frame;
		}
		else if (_textsCount < (int32_t)_textMotions.capacity)
		{
			textIndex = _textsCount++;
			_textMotions.items[textIndex].textAddress = textAddress;
			_textMotions.items[textIndex].textHash = textHash;
			_textMotions.items[textIndex].targetPos = posFromGameF;
			_textMotions.items[textIndex].currentPos = posFromGameF;
			_textMotions.items[textIndex].gamePos = posFromGame;
			_textMotions.items[textIndex].lastUsedFrame = _frame;
		}
		else
		{
			D2DX_DEBUG_LOG("TMP: Too many texts.");
		}
	}

	if (textIndex < 0)
	{
		return { 0, 0 };
	}

	TextMotion& tm = _textMotions.items[textIndex];
	return { (int32_t)(tm.currentPos.x - posFromGame.x), (int32_t)(tm.currentPos.y - posFromGame.y) };
}
