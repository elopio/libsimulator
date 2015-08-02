/*

Copyright (c) 2015, Arvid Norberg
All rights reserved.

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

*/

#include "simulator/simulator.hpp"
#include <functional>

typedef sim::chrono::high_resolution_clock::time_point time_point;
typedef sim::chrono::high_resolution_clock::duration duration;

namespace sim
{
	queue::queue(asio::io_service& ios
		, int bandwidth
		, chrono::high_resolution_clock::duration propagation_delay
		, int max_queue_size)
		: m_max_queue_size(max_queue_size)
		, m_forwarding_latency(propagation_delay)
		, m_bandwidth(bandwidth)
		, m_queue_size(0)
		, m_forward_timer(ios)
		, m_last_forward(chrono::high_resolution_clock::now())
	{}

	void queue::incoming_packet(aux::packet p)
	{
		const int packet_size = p.buffer.size() + p.overhead;

		// tail-drop
		if (p.ok_to_drop()
			&& m_max_queue_size > 0
			&& m_queue_size + packet_size > m_max_queue_size)
		{
			// if any hop on the network drops a packet, it has to return it to the
			// sender.
			boost::function<void(aux::packet)> drop_fun = p.drop_fun;
			if (drop_fun) drop_fun(std::move(p));
			return;
		}

		time_point now = chrono::high_resolution_clock::now();

		m_queue.push_back(std::make_pair(now + m_forwarding_latency, p));
		m_queue_size += packet_size;
		if (m_queue.size() > 1) return;

		begin_send_next_packet();
	}

	void queue::begin_send_next_packet()
	{
		time_point now = chrono::high_resolution_clock::now();

		if (m_queue.front().first > now)
		{
			m_forward_timer.expires_at(m_queue.front().first);
			m_forward_timer.async_wait(std::bind(&queue::begin_send_next_packet
				, this));
			return;
		}

		m_last_forward = now;
		if (m_bandwidth > 0)
		{
			const double nanoseconds_per_byte = 1000000000.0
				/ double(m_bandwidth);

			aux::packet const& p = m_queue.front().second;
			const int packet_size = p.buffer.size() + p.overhead;

			m_last_forward += chrono::duration_cast<duration>(chrono::nanoseconds(
				boost::int64_t(nanoseconds_per_byte * packet_size)));
		}

		m_forward_timer.expires_at(m_last_forward);
		m_forward_timer.async_wait(std::bind(&queue::next_packet_sent
			, this));
	}

	void queue::next_packet_sent()
	{
		aux::packet p = m_queue.front().second;
		const int packet_size = p.buffer.size() + p.overhead;
		m_queue_size -= packet_size;
		m_queue.erase(m_queue.begin());

		forward_packet(std::move(p));

		if (m_queue.size())
			begin_send_next_packet();
	}
}
