
	if($self->is_gsm()) {
		$self->{CODING} = 'ami';
		$self->{DCHAN} = ($self->chans())[$self->{DCHAN_IDX}];
		$self->{BCHANS} = [ ($self->chans())[@{$self->{BCHAN_LIST}}] ];
		# Infer some info from channel name:
		my $first_chan = ($self->chans())[0] || die "$0: No channels in span #$num\n";
		my $chan_fqn = $first_chan->fqn();
		if($chan_fqn =~ m(opvxg4xx.*/)) {				# OpenVox's GSM cards. 
			$self->{FRAMING} = 'ccs';
			$self->{SIGNALLING} = 'gsm';
		}
	}
