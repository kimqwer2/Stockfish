/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2022 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "partner.h"
#include "search.h"
#include "timeman.h"
#include "uci.h"

namespace Stockfish {

TimeManagement Time; // Our global time management object


/// TimeManagement::init() is called at the beginning of the search and calculates
/// the bounds of time allowed for the current game ply. We currently support:
//      1) x basetime (+ z increment)
//      2) x moves in y seconds (+ z increment)

void TimeManagement::init(const Position& pos, Search::LimitsType& limits, Color us, int ply) {

  TimePoint moveOverhead    = TimePoint(Options["Move Overhead"]);
  TimePoint slowMover       = TimePoint(Options["Slow Mover"]);
  TimePoint npmsec          = TimePoint(Options["nodestime"]);

  // optScale is a percentage of available time to use for the current move.
  // maxScale is a multiplier applied to optimumTime.
  double optScale, maxScale;

  // If we have to play in 'nodes as time' mode, then convert from time
  // to nodes, and use resulting values in time management formulas.
  // WARNING: to avoid time losses, the given npmsec (nodes per millisecond)
  // must be much lower than the real engine speed.
  if (npmsec)
  {
      if (!availableNodes) // Only once at game start
          availableNodes = npmsec * limits.time[us]; // Time is in msec

      // Convert from milliseconds to nodes
      limits.time[us] = TimePoint(availableNodes);
      limits.inc[us] *= npmsec;
      limits.npmsec = npmsec;
  }

  startTime = limits.startTime;

  if (limits.time[us] == 0)
  {
      optimumTime = maximumTime = 0;
      return;
  }

  // Maximum move horizon of 50 moves
  int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : 50;

  // If less than one second, gradually reduce move horizon
  const TimePoint scaledTime = npmsec ? limits.time[us] / npmsec : limits.time[us];
  if (scaledTime < 1000)
      mtg = int(scaledTime * 0.05);

  // Make sure timeLeft is > 0 since we may use it as a divisor
  TimePoint timeLeft =  std::max(TimePoint(1),
      limits.time[us] + limits.inc[us] * (mtg - 1) - moveOverhead * (2 + mtg));

  // Adjust time management for four-player variants
  if (pos.two_boards())
  {
      if (Partner.partnerDead && Partner.opptime)
          timeLeft -= Partner.opptime;
      else
      {
          timeLeft = std::min(timeLeft, 5000 + std::min(std::abs(limits.time[us] - Partner.opptime), TimePoint(Partner.opptime)));
          if (Partner.fast || Partner.partnerDead)
              timeLeft /= 4;
      }
  }

  // A user may scale time usage by setting UCI option "Slow Mover"
  // Default is 100 and changing this value will probably lose elo.
  timeLeft = slowMover * timeLeft / 100;

  // x basetime (+ z increment)
  // If there is a healthy increment, timeLeft can exceed the actual available
  // game time for the current move, so cap to a percentage of available game time.
  if (limits.movestogo == 0)
  {
      double timeAdjust = std::clamp(0.3272 * std::log10(double(timeLeft)) - 0.4141, 0.6, 1.5);

      double logTimeInSec = std::log10(std::max(1.0, double(scaledTime)) / 1000.0);
      double optConstant  = std::min(0.0029869 + 0.00033554 * logTimeInSec, 0.004905);
      double maxConstant  = std::max(3.3744 + 3.0608 * logTimeInSec, 3.1441);

      optScale = std::min(0.012112 + std::pow(ply + 3.22713, 0.46866) * optConstant,
                          0.19404 * limits.time[us] / double(timeLeft))
               * timeAdjust;
      maxScale = std::min(6.873, maxConstant + ply / 12.352);
  }

  // x moves in y seconds (+ z increment)
  else
  {
      optScale = std::min((0.88 + ply / 116.4) / mtg,
                          0.88 * limits.time[us] / double(timeLeft));
      maxScale = 1.3 + 0.11 * mtg;
  }

  optimumTime = TimePoint(std::max(1.0, optScale * timeLeft));
  maximumTime = TimePoint(std::max(double(optimumTime), std::min(0.8097 * limits.time[us] - moveOverhead,
                                                                  maxScale * optimumTime)));

  if (Options["Ponder"])
      optimumTime += optimumTime / 4;
}

} // namespace Stockfish
