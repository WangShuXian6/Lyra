import NextLink from 'next/link';
import type { ComponentProps } from 'react';

type Props = ComponentProps<'a'> & { prefetch?: boolean };

/** Static exports do not need speculative route-segment requests. */
export function SiteLink({ href = '', prefetch: _prefetch, ...props }: Props) {
  return <NextLink href={href} prefetch={false} {...props} />;
}
