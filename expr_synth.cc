/*
 * Copyright (c) 1999-2000 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#ifdef HAVE_CVS_IDENT
#ident "$Id: expr_synth.cc,v 1.42 2003/04/08 04:33:55 steve Exp $"
#endif

# include "config.h"

# include  <iostream>

# include  "netlist.h"
# include  "netmisc.h"

NetNet* NetExpr::synthesize(Design*des)
{
      cerr << get_line() << ": internal error: cannot synthesize expression: "
	   << *this << endl;
      des->errors += 1;
      return 0;
}

/*
 * Make an LPM_ADD_SUB device from addition operators.
 */
NetNet* NetEBAdd::synthesize(Design*des)
{
      assert((op()=='+') || (op()=='-'));

      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);
      
      assert(lsig->pin_count() == rsig->pin_count());
      unsigned width=lsig->pin_count();

      string path = lsig->scope()->local_symbol();
      NetNet*osig = new NetNet(lsig->scope(), path, NetNet::IMPLICIT, width);
      osig->local_flag(true);

      string oname = osig->scope()->local_symbol();
      NetAddSub *adder = new NetAddSub(lsig->scope(), oname, width);
      for (unsigned idx = 0 ;  idx < width;  idx += 1) {
	    connect(lsig->pin(idx), adder->pin_DataA(idx));
	    connect(rsig->pin(idx), adder->pin_DataB(idx));
	    connect(osig->pin(idx), adder->pin_Result(idx));
      }
      des->add_node(adder);

      switch (op()) {
	  case '+':
	    adder->attribute("LPM_Direction", verinum("ADD"));
	    break;
	  case '-':
	    adder->attribute("LPM_Direction", verinum("SUB"));
	    break;
      }

      return osig;
}

/*
 * The bitwise logic operators are turned into discrete gates pretty
 * easily. Synthesize the left and right sub-expressions to get
 * signals, then just connect a single gate to each bit of the vector
 * of the expression.
 */
NetNet* NetEBBits::synthesize(Design*des)
{
      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);

      NetScope*scope = lsig->scope();
      assert(scope);
      string path = des->local_symbol(scope->name());

      if (lsig->pin_count() != rsig->pin_count()) {
	    cerr << get_line() << ": internal error: bitwise (" << op_
		 << ") widths do not match: " << lsig->pin_count()
		 << " != " << rsig->pin_count() << endl;
	    cerr << get_line() << ":               : width="
		 << lsig->pin_count() << ": " << *left_ << endl;
	    cerr << get_line() << ":               : width="
		 << rsig->pin_count() << ": " << *right_ << endl;
	    return 0;
      }

      assert(lsig->pin_count() == rsig->pin_count());
      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, lsig->pin_count());
      osig->local_flag(true);

      for (unsigned idx = 0 ;  idx < osig->pin_count() ;  idx += 1) {
	    string oname = scope->local_hsymbol();
	    NetLogic*gate;

	      /* If the rsig bit is constant, then look for special
		 cases that I can use to reduce the generated
		 logic. If I find one, then handle it immediately and
		 skip the rest of the processing of this bit. */
	    if (rsig->pin(idx).nexus()->drivers_constant()) {
		  verinum::V bval = rsig->pin(idx).nexus()->driven_value();

		    /* (A & 0) is (0) */
		  if ((op() == '&') && bval == verinum::V0) {
			connect(osig->pin(idx), rsig->pin(idx));
			continue;
		  }

		    /* (A & 1) is A */
		  if ((op() == '&') && bval == verinum::V1) {
			connect(osig->pin(idx), lsig->pin(idx));
			continue;
		  }
	    }

	    switch (op()) {
		case '&':
		  gate = new NetLogic(scope, oname, 3, NetLogic::AND);
		  break;
		case '|':
		  gate = new NetLogic(scope, oname, 3, NetLogic::OR);
		  break;
		case '^':
		  gate = new NetLogic(scope, oname, 3, NetLogic::XOR);
		  break;
		case 'O':
		  gate = new NetLogic(scope, oname, 3, NetLogic::NOR);
		  break;
		case 'X':
		  gate = new NetLogic(scope, oname, 3, NetLogic::XNOR);
		  break;
		default:
		  assert(0);
	    }

	    connect(osig->pin(idx), gate->pin(0));
	    connect(lsig->pin(idx), gate->pin(1));
	    connect(rsig->pin(idx), gate->pin(2));

	    des->add_node(gate);
      }

      return osig;
}

NetNet* NetEBComp::synthesize(Design*des)
{
      NetEConst*lcon = reinterpret_cast<NetEConst*>(left_);
      NetEConst*rcon = reinterpret_cast<NetEConst*>(right_);

	/* Handle the special case where one of the inputs is constant
	   0. We can use an OR gate to do the comparison. Synthesize
	   the non-const side as normal, then or(nor) the signals
	   together to get result. */
      if ((rcon && (rcon->value() == verinum(0UL,rcon->expr_width())))
	  || (lcon && (lcon->value() == verinum(0UL,lcon->expr_width())))) {

	    NetNet*lsig = rcon
		  ? left_->synthesize(des)
		  : right_->synthesize(des);
	    NetScope*scope = lsig->scope();
	    assert(scope);

	    NetNet*osig = new NetNet(scope, scope->local_symbol(),
				     NetNet::IMPLICIT, 1);
	    osig->local_flag(true);

	    NetLogic*gate;
	    switch (op_) {
		case 'e':
		case 'E':
		  gate = new NetLogic(scope, scope->local_hsymbol(),
				      lsig->pin_count()+1, NetLogic::NOR);
		  break;
		case 'n':
		case 'N':
		  gate = new NetLogic(scope, scope->local_hsymbol(),
				      lsig->pin_count()+1, NetLogic::OR);
		  break;

		case '>':
		    /* sig > 0 is true if any bit in sig is set. This
		       is very much like sig != 0. (0 > sig) shouldn't
		       happen. */
		  if (rcon) {
			gate = new NetLogic(scope, scope->local_hsymbol(),
					    lsig->pin_count()+1, NetLogic::OR);
		  } else {
			assert(0);
			gate = new NetLogic(scope, scope->local_hsymbol(),
				      lsig->pin_count()+1, NetLogic::NOR);
		  }
		  break;

		case '<':
		    /* 0 < sig is handled like sig > 0. */
		  if (! rcon) {
			gate = new NetLogic(scope, scope->local_hsymbol(),
					    lsig->pin_count()+1, NetLogic::OR);
		  } else {
			assert(0);
			gate = new NetLogic(scope, scope->local_hsymbol(),
				      lsig->pin_count()+1, NetLogic::NOR);
		  }
		  break;

		default:
		  assert(0);
	    }

	    connect(gate->pin(0), osig->pin(0));
	    for (unsigned idx = 0 ;  idx < lsig->pin_count() ;  idx += 1)
		  connect(gate->pin(idx+1), lsig->pin(idx));

	    des->add_node(gate);
	    return osig;
      }

      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);

      NetScope*scope = lsig->scope();
      assert(scope);

      unsigned width = lsig->pin_count();
      if (rsig->pin_count() > lsig->pin_count())
	    width = rsig->pin_count();

      lsig = pad_to_width(des, lsig, width);
      rsig = pad_to_width(des, rsig, width);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, 1);
      osig->local_flag(true);

	/* Handle the special case of a single bit equality
	   operation. Make an XNOR gate instead of a comparator. */
      if ((width == 1) && ((op_ == 'e') || (op_ == 'E'))) {
	    NetLogic*gate = new NetLogic(scope, scope->local_hsymbol(),
					 3, NetLogic::XNOR);
	    connect(gate->pin(0), osig->pin(0));
	    connect(gate->pin(1), lsig->pin(0));
	    connect(gate->pin(2), rsig->pin(0));
	    des->add_node(gate);
	    return osig;
      }

	/* Handle the special case of a single bit inequality
	   operation. This is similar to single bit equality, but uses
	   an XOR instead of an XNOR gate. */
      if ((width == 1) && ((op_ == 'n') || (op_ == 'N'))) {
	    NetLogic*gate = new NetLogic(scope, scope->local_hsymbol(),
					 3, NetLogic::XOR);
	    connect(gate->pin(0), osig->pin(0));
	    connect(gate->pin(1), lsig->pin(0));
	    connect(gate->pin(2), rsig->pin(0));
	    des->add_node(gate);
	    return osig;
      }


      NetCompare*dev = new NetCompare(scope, scope->local_symbol(), width);
      des->add_node(dev);

      for (unsigned idx = 0 ;  idx < lsig->pin_count() ;  idx += 1)
	    connect(dev->pin_DataA(idx), lsig->pin(idx));

      for (unsigned idx = 0 ;  idx < rsig->pin_count() ;  idx += 1)
	    connect(dev->pin_DataB(idx), rsig->pin(idx));


      switch (op_) {
	  case '<':
	    connect(dev->pin_ALB(), osig->pin(0));
	    break;
	  case '>':
	    connect(dev->pin_AGB(), osig->pin(0));
	    break;
	  case 'e': // ==
	  case 'E': // === ?
	    connect(dev->pin_AEB(), osig->pin(0));
	    break;
	  case 'G': // >=
	    connect(dev->pin_AGEB(), osig->pin(0));
	    break;
	  case 'L': // <=
	    connect(dev->pin_ALEB(), osig->pin(0));
	    break;
	  case 'n': // !=
	  case 'N': // !==
	    connect(dev->pin_ANEB(), osig->pin(0));
	    break;

	  default:
	    cerr << get_line() << ": internal error: cannot synthesize "
		  "comparison: " << *this << endl;
	    des->errors += 1;
	    return 0;
      }

      return osig;
}

NetNet* NetEBDiv::synthesize(Design*des)
{
      cerr << get_line() << ": internal error: cannot synthesize division: "
	   << *this << endl;
      des->errors += 1;
      return 0;
}

NetNet* NetEBLogic::synthesize(Design*des)
{
      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);

      if (lsig == 0)
	    return 0;

      if (rsig == 0)
	    return 0;

      NetScope*scope = lsig->scope();
      assert(scope);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, 1);
      osig->local_flag(true);


      if (op() == 'o') {

	      /* Logic OR can handle the reduction *and* the logical
		 comparison with a single wide OR gate. So handle this
		 magically. */

	    string oname = scope->local_hsymbol();

	    NetLogic*olog = new NetLogic(scope, oname,
					 lsig->pin_count()+rsig->pin_count()+1,
					 NetLogic::OR);

	    connect(osig->pin(0), olog->pin(0));

	    unsigned pin = 1;
	    for (unsigned idx = 0 ;  idx < lsig->pin_count() ;  idx = 1)
		  connect(olog->pin(pin+idx), lsig->pin(idx));

	    pin += lsig->pin_count();
	    for (unsigned idx = 0 ;  idx < rsig->pin_count() ;  idx = 1)
		  connect(olog->pin(pin+idx), rsig->pin(idx));

	    des->add_node(olog);

      } else {
	    assert(op() == 'a');

	      /* Create the logic AND gate. This is a single bit
		 output, with inputs for each of the operands. */
	    NetLogic*olog;
	    string oname = scope->local_hsymbol();

	    olog = new NetLogic(scope, oname, 3, NetLogic::AND);

	    connect(osig->pin(0), olog->pin(0));
	    des->add_node(olog);

	      /* XXXX Here, I need to reduce the parameters with
		 reduction or. */


	      /* By this point, the left and right parameters have been
		 reduced to single bit values. Now we just connect them to
		 the logic gate. */
	    assert(lsig->pin_count() == 1);
	    connect(lsig->pin(0), olog->pin(1));

	    assert(rsig->pin_count() == 1);
	    connect(lsig->pin(0), olog->pin(2));
      }


      return osig;
}

NetNet* NetEBShift::synthesize(Design*des)
{
      NetNet*lsig = left_->synthesize(des);
      NetNet*rsig = right_->synthesize(des);

      if (lsig == 0)
	    return 0;

      if (rsig == 0)
	    return 0;

      NetScope*scope = lsig->scope();

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, expr_width());

      assert(op() == 'l');
      NetCLShift*dev = new NetCLShift(scope, scope->local_symbol(),
				      osig->pin_count(),
				      rsig->pin_count());
      des->add_node(dev);

      for (unsigned idx = 0 ; idx < dev->width() ;  idx += 1)
	    connect(dev->pin_Result(idx), osig->pin(idx));

      assert(lsig->pin_count() >= dev->width());
      for (unsigned idx = 0 ;  idx < dev->width() ;  idx += 1)
	    connect(dev->pin_Data(idx), lsig->pin(idx));

      for (unsigned idx = 0 ;  idx < dev->width_dist() ;  idx += 1)
	    connect(dev->pin_Distance(idx), rsig->pin(idx));

      verinum dir_v = (op() == 'r')? verinum::V1 : verinum::V0;
      NetNet*dir_n = new NetNet(scope, scope->local_symbol(),
				NetNet::WIRE, 1);
      NetConst*dir = new NetConst(scope, scope->local_symbol(), dir_v);
      connect(dev->pin_Direction(), dir->pin(0));
      connect(dev->pin_Direction(), dir_n->pin(0));

      return osig;
}

NetNet* NetEConcat::synthesize(Design*des)
{
	/* First, synthesize the operands. */
      NetNet**tmp = new NetNet*[parms_.count()];
      for (unsigned idx = 0 ;  idx < parms_.count() ;  idx += 1)
	    tmp[idx] = parms_[idx]->synthesize(des);

      assert(tmp[0]);
      NetScope*scope = tmp[0]->scope();
      assert(scope);

	/* Make a NetNet object to carry the output vector. */
      string path = scope->local_symbol();
      NetNet*osig = new NetNet(scope, path, NetNet::IMPLICIT, expr_width());
      osig->local_flag(true);

	/* Connect the output vector to the operands. */
      unsigned obit = 0;
      for (unsigned idx = parms_.count() ;  idx > 0 ;  idx -= 1) {

	    assert(tmp[idx-1]);

	    for (unsigned bit = 0;  bit < tmp[idx-1]->pin_count(); bit += 1) {
		  connect(osig->pin(obit), tmp[idx-1]->pin(bit));
		  obit += 1;
	    }

	    if (tmp[idx-1]->local_flag() && tmp[idx-1]->get_refs() == 0)
		  delete tmp[idx-1];
      }

      delete[]tmp;
      return osig;
}

NetNet* NetEConst::synthesize(Design*des)
{
      NetScope*scope = des->find_root_scope();
      assert(scope);

      string path = scope->local_symbol();
      unsigned width=expr_width();

      NetNet*osig = new NetNet(scope, path, NetNet::IMPLICIT, width);
      osig->local_flag(true);
      NetConst*con = new NetConst(scope, scope->local_symbol(), value());
      for (unsigned idx = 0 ;  idx < width;  idx += 1)
	    connect(osig->pin(idx), con->pin(idx));

      des->add_node(con);
      return osig;
}

NetNet* NetECReal::synthesize(Design*des)
{
      cerr << get_line() << ": error: Real constants are "
	   << "not synthesizable." << endl;
      des->errors += 1;
      return 0;
}

/*
 * The bitwise unary logic operator (there is only one) is turned
 * into discrete gates just as easily as the binary ones above.
 */
NetNet* NetEUBits::synthesize(Design*des)
{
      NetNet*isig = expr_->synthesize(des);

      NetScope*scope = isig->scope();
      assert(scope);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, isig->pin_count());
      osig->local_flag(true);

      for (unsigned idx = 0 ;  idx < osig->pin_count() ;  idx += 1) {
	    string oname = scope->local_hsymbol();
	    NetLogic*gate;

	    switch (op()) {
		case '~':
		  gate = new NetLogic(scope, oname, 2, NetLogic::NOT);
		  break;
		default:
		  assert(0);
	    }

	    connect(osig->pin(idx), gate->pin(0));
	    connect(isig->pin(idx), gate->pin(1));

	    des->add_node(gate);
      }

      return osig;
}

NetNet* NetEUReduce::synthesize(Design*des)
{
      NetNet*isig = expr_->synthesize(des);

      NetScope*scope = isig->scope();
      assert(scope);

      NetNet*osig = new NetNet(scope, scope->local_symbol(),
			       NetNet::IMPLICIT, 1);
      osig->local_flag(true);

      string oname = scope->local_hsymbol();
      NetLogic*gate;

      switch (op()) {
	  case 'N':
	  case '!':
	    gate = new NetLogic(scope, oname, isig->pin_count()+1,
				NetLogic::NOR);
	    break;

	  case '&':
	    gate = new NetLogic(scope, oname, isig->pin_count()+1,
				NetLogic::AND);
	    break;

	  case '|':
	    gate = new NetLogic(scope, oname, isig->pin_count()+1,
				NetLogic::OR);
	    break;

	  case '^':
	    gate = new NetLogic(scope, oname, isig->pin_count()+1,
				NetLogic::XOR);
	    break;

	  case 'A':
	    gate = new NetLogic(scope, oname, isig->pin_count()+1,
				NetLogic::NAND);
	    break;

	  case 'X':
	    gate = new NetLogic(scope, oname, isig->pin_count()+1,
				NetLogic::XNOR);
	    break;

	  default:
	    cerr << get_line() << ": internal error: "
		 << "Unable to synthesize " << *this << "." << endl;
	    return 0;
      }

      des->add_node(gate);
      connect(gate->pin(0), osig->pin(0));
      for (unsigned idx = 0 ;  idx < isig->pin_count() ;  idx += 1)
	    connect(gate->pin(1+idx), isig->pin(idx));

      return osig;
}

/*
 * Synthesize a ?: operator an a NetMux device. Connect the condition
 * expression to the select input, then connect the true and false
 * expressions to the B and A inputs. This way, when the select input
 * is one, the B input, which is the true expression, is selected.
 */
NetNet* NetETernary::synthesize(Design *des)
{
      NetNet*csig = cond_->synthesize(des);
      NetNet*tsig = true_val_->synthesize(des);
      NetNet*fsig = false_val_->synthesize(des);

      string path = csig->scope()->local_symbol();

      assert(csig->pin_count() == 1);
      assert(tsig->pin_count() == fsig->pin_count());
      unsigned width=tsig->pin_count();
      NetNet*osig = new NetNet(csig->scope(), path, NetNet::IMPLICIT, width);
      osig->local_flag(true);

      string oname = csig->scope()->local_symbol();
      NetMux *mux = new NetMux(csig->scope(), oname, width, 2, 1);
      for (unsigned idx = 0 ;  idx < width;  idx += 1) {
	    connect(tsig->pin(idx), mux->pin_Data(idx, 1));
	    connect(fsig->pin(idx), mux->pin_Data(idx, 0));
	    connect(osig->pin(idx), mux->pin_Result(idx));
      }
      des->add_node(mux);
      connect(csig->pin(0), mux->pin_Sel(0));

      return osig;
}

/*
 * When synthesizing a signal expression, it is usually fine to simply
 * return the NetNet that it refers to. If this is a part select,
 * though, a bit more work needs to be done. Return a temporary that
 * represents the connections to the selected bits.
 *
 * For example, if there is a reg foo, like so:
 *     reg [5:0] foo;
 * and this expression node represents a part select foo[3:2], then
 * create a temporary like so:
 *
 *                     foo
 *                    +---+
 *                    | 5 |
 *                    +---+
 *         tmp        | 4 |
 *        +---+       +---+
 *        | 1 | <---> | 3 |
 *        +---+       +---+
 *        | 0 | <---> | 2 |
 *        +---+       +---+
 *                    | 1 |
 *                    +---+
 *                    | 0 |
 *                    +---+
 * The temporary is marked as a temporary and returned to the
 * caller. This causes the caller to get only the selected part of the
 * signal, and when it hooks up to tmp, it hooks up to the right parts
 * of foo.
 */
NetNet* NetESignal::synthesize(Design*des)
{
      if ((lsi_ == 0) && (msi_ == (net_->pin_count() - 1)))
	    return net_;

      assert(msi_ >= lsi_);
      unsigned wid = msi_ - lsi_ + 1;

      NetScope*scope = net_->scope();
      assert(scope);

      string name = scope->local_symbol();
      NetNet*tmp = new NetNet(scope, name, NetNet::NetNet::WIRE, wid);
      tmp->local_flag(true);

      for (unsigned idx = 0 ;  idx < wid ;  idx += 1)
	    connect(tmp->pin(idx), net_->pin(idx+lsi_));

      return tmp;
}

/*
 * $Log: expr_synth.cc,v $
 * Revision 1.42  2003/04/08 04:33:55  steve
 *  Synthesize shift expressions.
 *
 * Revision 1.41  2003/03/06 00:28:41  steve
 *  All NetObj objects have lex_string base names.
 *
 * Revision 1.40  2003/02/26 01:29:24  steve
 *  LPM objects store only their base names.
 *
 * Revision 1.39  2003/01/30 16:23:07  steve
 *  Spelling fixes.
 *
 * Revision 1.38  2003/01/26 21:15:58  steve
 *  Rework expression parsing and elaboration to
 *  accommodate real/realtime values and expressions.
 *
 * Revision 1.37  2002/11/17 23:37:55  steve
 *  Magnitude compare to 0.
 *
 * Revision 1.36  2002/08/12 01:34:59  steve
 *  conditional ident string using autoconfig.
 *
 * Revision 1.35  2002/07/07 22:31:39  steve
 *  Smart synthesis of binary AND expressions.
 *
 * Revision 1.34  2002/07/05 21:26:17  steve
 *  Avoid emitting to vvp local net symbols.
 *
 * Revision 1.33  2002/05/26 01:39:02  steve
 *  Carry Verilog 2001 attributes with processes,
 *  all the way through to the ivl_target API.
 *
 *  Divide signal reference counts between rval
 *  and lval references.
 */

