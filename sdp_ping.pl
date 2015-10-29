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
my $num_boards;   # number of chips
my $ip;     # board ip
my $board_type; # board type (3 for spinn3 or 5 for spinn5)
my $cpu=1;

# Process the five arguments and open the connection to SpiNNaker
# The arguments are
#   hostname (or IP address) of the SpiNNaker system
#   X coordinate of the SpiNNaker chip
#   Y coordinate of the SpiNNaker chip
#   core number on the SpiNNaker chip
sub process_args
{
  die "usage: sdp_ping <hostname>  <board type (3/5)> <num boards>\n" unless
    $#ARGV == 2 &&
    $ARGV[1] =~ /^\d+$/ &&
    $ARGV[2] =~ /^\d+$/;

  $ip         = $ARGV[0];
  $board_type = $ARGV[1];
  $num_boards = $ARGV[2];
  $port       = 1;

  $spin = SpiNN::SCP->new(target => $ip);
  die "Failed to connect to $ip\n" unless $spin;
}

sub probe_chips
{
  my ($x_max, $y_max) = @_;
  my ($x, $y);
  my $pad = pack "V4", 0, 0, 0, 0;

  for($x=0; $x<$x_max; $x++)
  {
    for($y=0; $y<$y_max; $y++)
    {
      $spin->addr($x, $y, $cpu);
      $spin->send_sdp($pad, port => $port, reply => ($port == 1), debug => $debug);

      my $rc = $spin->recv_sdp (timeout => 0.1, debug => $debug);
      if ($rc) {
        print "$spin->{sdp_data}\n";
      }
      else {
        print "# No reply ($x,$y)\n";
      }
    }
  }
}

# Main loop which sends a ping SDP packet every "$sleep" seconds and looks
# for incoming reply packets. Both sent and received packets are printed
# using the SpiNN::SCP debug facility. We expect a reply only if we send
# to port 1 
sub main
{
  process_args();

  if ($board_type==5) {
    if ($num_boards==1) {
      print "This will probe chips that don't exist because machine is not square/rectangular.\n";
      probe_chips(8,8);
    }
    elsif ($num_boards==3) {
      probe_chips(12,12);
    }
    elsif ($num_boards==6) {
      probe_chips(24,12);
    }
    elsif ($num_boards==12) {
      die "Not yet implemented\n";
    }
    elsif ($num_boards==24) {
      probe_chips(48,24);
    }
    elsif ($num_boards==120) {
      probe_chips(96,60);
    }
  }
  elsif ($board_type==3) # ignore board size
  {
    probe_chips(2,2);
  }

}

main ();

#------------------------------------------------------------------------------
