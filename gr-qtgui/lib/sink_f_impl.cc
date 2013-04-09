/* -*- c++ -*- */
/*
 * Copyright 2008-2012 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "sink_f_impl.h"
#include <gr_io_signature.h>
#include <string.h>
#include <volk/volk.h>

namespace gr {
  namespace qtgui {
    
    sink_f::sptr
    sink_f::make(int fftsize, int wintype,
		 double fc, double bw,
		 const std::string &name,
		 bool plotfreq, bool plotwaterfall,
		 bool plottime, bool plotconst,
		 QWidget *parent)
    {
      return gnuradio::get_initial_sptr
	(new sink_f_impl(fftsize, wintype,
			 fc, bw, name,
			 plotfreq, plotwaterfall,
			 plottime, plotconst,
			 parent));
    }

    sink_f_impl::sink_f_impl(int fftsize, int wintype,
			     double fc, double bw,
			     const std::string &name,
			     bool plotfreq, bool plotwaterfall,
			     bool plottime, bool plotconst,
			     QWidget *parent)
      : gr_block("sink_f",
		 gr_make_io_signature(1, 1, sizeof(float)),
		 gr_make_io_signature (0, 0, 0)),
	d_fftsize(fftsize),
	d_wintype((filter::firdes::win_type)(wintype)),
	d_center_freq(fc), d_bandwidth(bw), d_name(name),
	d_plotfreq(plotfreq), d_plotwaterfall(plotwaterfall),
	d_plottime(plottime), d_plotconst(plotconst),
	d_parent(parent)
    {
      d_main_gui = NULL;

      // Perform fftshift operation;
      // this is usually desired when plotting
      d_shift = true;

      d_fft = new fft::fft_complex (d_fftsize, true);

      d_index = 0;
      d_residbuf = new float[d_fftsize];
      d_magbuf = new float[d_fftsize];

      buildwindow();

      initialize();
    }

    sink_f_impl::~sink_f_impl()
    {
      delete d_main_gui;
      delete [] d_residbuf;
      delete [] d_magbuf;
      delete d_fft;
    }

    bool
    sink_f_impl::check_topology(int ninputs, int noutputs)
    {
      return ninputs == 1;
    }

    void
    sink_f_impl::forecast(int noutput_items, gr_vector_int &ninput_items_required)
    {
      unsigned int ninputs = ninput_items_required.size();
      for (unsigned int i = 0; i < ninputs; i++) {
	ninput_items_required[i] = std::min(d_fftsize, 8191);
      }
    }

    void
    sink_f_impl::initialize()
    {
      if(qApp != NULL) {
	d_qApplication = qApp;
      }
      else {
	int argc;
	char **argv = NULL;
	d_qApplication = new QApplication(argc, argv);
      }

      uint64_t maxBufferSize = 32768;
      d_main_gui = new SpectrumGUIClass(maxBufferSize, d_fftsize,
					d_center_freq,
					-d_bandwidth/2.0,
					d_bandwidth/2.0);
      d_main_gui->setDisplayTitle(d_name);
      d_main_gui->setWindowType((int)d_wintype);
      set_fft_size(d_fftsize);

      d_main_gui->openSpectrumWindow(d_parent,
				     d_plotfreq, d_plotwaterfall,
				     d_plottime, d_plotconst);

      // initialize update time to 10 times a second
      set_update_time(0.1);
    }

    void
    sink_f_impl::exec_()
    {
      d_qApplication->exec();
    }

    QWidget*
    sink_f_impl::qwidget()
    {
      return d_main_gui->qwidget();
    }

    PyObject*
    sink_f_impl::pyqwidget()
    {
      PyObject *w = PyLong_FromVoidPtr((void*)d_main_gui->qwidget());
      PyObject *retarg = Py_BuildValue("N", w);
      return retarg;
    }

    void
    sink_f_impl::set_fft_size(const int fftsize)
    {
      d_fftsize = fftsize;
      d_main_gui->setFFTSize(fftsize);
    }

    int
    sink_f_impl::fft_size() const
    {
      return d_fftsize;
    }

    void
    sink_f_impl::set_frequency_range(const double centerfreq,
				     const double bandwidth)
    {
      d_center_freq = centerfreq;
      d_bandwidth = bandwidth;
      d_main_gui->setFrequencyRange(d_center_freq,
				    -d_bandwidth/2.0,
				    d_bandwidth/2.0);
    }

    void
    sink_f_impl::set_fft_power_db(double min, double max)
    {
      d_main_gui->setFrequencyAxis(min, max);
    }

    /*
    void
    sink_f_impl::set_time_domain_axis(double min, double max)
    {
      d_main_gui->setTimeDomainAxis(min, max);
    }

    void
    sink_f_impl::set_constellation_axis(double xmin, double xmax,
					double ymin, double ymax)
    {
      d_main_gui->setConstellationAxis(xmin, xmax, ymin, ymax);
    }

    void
    sink_f_impl::set_constellation_pen_size(int size)
    {
      d_main_gui->setConstellationPenSize(size);
    }
    */

    void
    sink_f_impl::set_update_time(double t)
    {
      d_update_time = t;
      d_main_gui->setUpdateTime(d_update_time);
    }

    void
    sink_f_impl::fft(float *data_out, const float *data_in, int size)
    {
      if (d_window.size()) {
	gr_complex *dst = d_fft->get_inbuf();
	for (int i = 0; i < size; i++)		// apply window
	  dst[i] = data_in[i] * d_window[i];
      }
      else {
	gr_complex *dst = d_fft->get_inbuf();
	for (int i = 0; i < size; i++)	        // float to complex conversion
	  dst[i] = data_in[i];
      }

      d_fft->execute ();     // compute the fft
      volk_32fc_s32f_x2_power_spectral_density_32f_a(data_out, d_fft->get_outbuf(),
						     size, 1.0, size);
    }

    void
    sink_f_impl::windowreset()
    {
      filter::firdes::win_type newwintype;
      newwintype = (filter::firdes::win_type)d_main_gui->getWindowType();
      if(d_wintype != newwintype) {
	d_wintype = newwintype;
	buildwindow();
      }
    }

    void
    sink_f_impl::buildwindow()
    {
      d_window.clear();
      if(d_wintype != 0) {
	d_window = filter::firdes::window(d_wintype, d_fftsize, 6.76);
      }
    }

    void
    sink_f_impl::fftresize()
    {
      int newfftsize = d_main_gui->getFFTSize();

      if(newfftsize != d_fftsize) {

	// Resize residbuf and replace data
	delete [] d_residbuf;
	d_residbuf = new float[newfftsize];

	delete [] d_magbuf;
	d_magbuf = new float[newfftsize];

	// Set new fft size and reset buffer index 
	// (throws away any currently held data, but who cares?) 
	d_fftsize = newfftsize;
	d_index = 0;

	// Reset window to reflect new size
	buildwindow();

	// Reset FFTW plan for new size
	delete d_fft;
	d_fft = new fft::fft_complex (d_fftsize, true);
      }
    }

    int
    sink_f_impl::general_work(int noutput_items,
			      gr_vector_int &ninput_items,
			      gr_vector_const_void_star &input_items,
			      gr_vector_void_star &output_items)
    {
      int j=0;
      const float *in = (const float*)input_items[0];

      // Update the FFT size from the application
      fftresize();
      windowreset();

      for(int i=0; i < noutput_items; i+=d_fftsize) {
	unsigned int datasize = noutput_items - i;
	unsigned int resid = d_fftsize-d_index;

	// If we have enough input for one full FFT, do it
	if(datasize >= resid) {
	  const gr::high_res_timer_type currentTime = gr::high_res_timer_now();

	  // Fill up residbuf with d_fftsize number of items
	  memcpy(d_residbuf+d_index, &in[j], sizeof(float)*resid);
	  d_index = 0;

	  j += resid;
	  fft(d_magbuf, d_residbuf, d_fftsize);
      
	  d_main_gui->updateWindow(true, d_magbuf, d_fftsize,
				   (float*)d_residbuf, d_fftsize, NULL, 0,
				   currentTime, true);
	}
	// Otherwise, copy what we received into the residbuf for next time
	else {
	  memcpy(d_residbuf+d_index, &in[j], sizeof(float)*datasize);
	  d_index += datasize;
	  j += datasize;
	}
      }

      consume_each(j);
      return j;
    }

  } /* namespace qtgui */
} /* namespace gr */