#!/usr/bin/perl

##------------------------------------------------------------------------------
##
## sdp_ping	    SDP ping program
##
## Copyright (C)    The University of Manchester - 2014
##
## Author           Steve Temple, APT Group, School of Computer Science
## Email            temples@cs.man.ac.uk
##
##------------------------------------------------------------------------------


use strict;
use warnings;

use SpiNN::SCP;

my $sleep = 0.2;
my $debug = 1;		# Debug level (3 or 4)

my $spin;		# SpiNNaker handle
my $port;		# SpiNNaker app. port


# Process the five arguments and open the connection to SpiNNaker
# The arguments are
#   hostname (or IP address) of the SpiNNaker system
#   X coordinate of the SpiNNaker chip
#   Y coordinate of the SpiNNaker chip
#   core number on the SpiNNaker chip

sub process_args
{
  die "usage: sdp_ping <hostname> <chipX> <chipY> <CPU>\n" unless
		$#ARGV == 3 &&
		$ARGV[1] =~ /^\d+$/ &&
		$ARGV[2] =~ /^\d+$/ &&
		$ARGV[3] =~ /^\d+$/;

  $spin = SpiNN::SCP->new (target => $ARGV[0]);
  die "Failed to connect to $ARGV[0]\n" unless $spin;

  $spin->addr ($ARGV[1], $ARGV[2], $ARGV[3]);

  $port = 1;
}


# Main loop which sends a ping SDP packet every "$sleep" seconds and looks
# for incoming reply packets. Both sent and received packets are printed
# using the SpiNN::SCP debug facility. We expect a reply only if we send
# to port 1 

sub main
{
  process_args();

  my $pad = pack "V4", 0, 0, 0, 0;

	$spin->send_sdp($pad, port => $port, reply => ($port == 1), debug => $debug);

	if ($debug>=3) {
		print "\n";
	}

	my $rc = $spin->recv_sdp (timeout => 0.1, debug => $debug);
	if ($rc)
	{
		print "$spin->{sdp_data}\n";
	}
	else
	{
		print "# No reply\n";
	}
}

main ();

#------------------------------------------------------------------------------
