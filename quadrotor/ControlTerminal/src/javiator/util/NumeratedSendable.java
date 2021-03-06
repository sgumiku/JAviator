/*****************************************************************************/
/*   This code is part of the JAviator project: javiator.cs.uni-salzburg.at  */
/*                                                                           */
/*   Copyright (c) 2006-2013 Harald Roeck <harald.roeck@gmail.com>           */
/*                                                                           */
/*   This program is free software; you can redistribute it and/or modify    */
/*   it under the terms of the GNU General Public License as published by    */
/*   the Free Software Foundation; either version 2 of the License, or       */
/*   (at your option) any later version.                                     */
/*                                                                           */
/*   This program is distributed in the hope that it will be useful,         */
/*   but WITHOUT ANY WARRANTY; without even the implied warranty of          */
/*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           */
/*   GNU General Public License for more details.                            */
/*                                                                           */
/*   You should have received a copy of the GNU General Public License       */
/*   along with this program; if not, write to the Free Software Foundation, */
/*   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.      */
/*                                                                           */
/*****************************************************************************/

package javiator.util;

public abstract class NumeratedSendable implements Sendable, Copyable {

	public void copyTo(Copyable copy)
	{
		((NumeratedSendable) copy).sequence = sequence;
	}

	protected long sequence;
	public long getSequence() {
		return sequence;
	}

	public void setSequence(long seq) {
		sequence = seq;
	}

	public Object clone( )  {
		NumeratedSendable copy;
		
		try {
			copy = (NumeratedSendable) super.clone();
		} catch (CloneNotSupportedException e) {
			// TODO Auto-generated catch block
			copy = null;
		}
		
		
		return copy;
	}
}
