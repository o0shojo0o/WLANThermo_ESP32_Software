/*************************************************** 
    Copyright (C) 2019  Martin Koerner

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    
    HISTORY: Please refer Github History
    
****************************************************/
#pragma once

#include "TemperatureBase.h"

class TemperatureMcp3208 : public TemperatureBase
{
public:
  TemperatureMcp3208();
  TemperatureMcp3208(uint8_t index, uint8_t csPin);
  void update();

private:
  uint16_t readChip();
  uint8_t csPin;
};
